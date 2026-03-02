/***************************
@Author: Copilot
@File: T30-RegionDynamic.cpp
@Time: 2026/3/2
@Desc: 演示先注册GRegion，再通过RegionAspect在run前动态创建子节点及依赖
***************************/

#include "MyGNode/MyNode1.h"
#include "MyGNode/MyNode2.h"

using namespace CGraph;

class RegionDynamicBuildParam : public GAspectParam {
public:
    CVoid clone(GPassedParam* param) override {
        auto* cur = dynamic_cast<RegionDynamicBuildParam*>(param);
        if (nullptr == cur) {
            return;
        }

        region_ = cur->region_;
        injected_ = cur->injected_;
    }

    GRegionPtr region_ { nullptr };
    CBool injected_ { false };
};


class RegionDynamicBuildAspect : public GAspect {
public:
    CStatus beginRun() override {
        CGRAPH_FUNCTION_BEGIN
        auto* param = this->getAParam<RegionDynamicBuildParam>();
        CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION(nullptr == param || nullptr == param->region_,
                                                "region dynamic build aspect param is invalid")

        if (param->injected_) {
            return status;
        }

        // 在region真正执行前，动态创建并注册内部节点及依赖
        auto* b1 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({}, "nodeB1_dynamic", 1));
        auto* b2 = param->region_->enqueueDynamicNode<MyNode2>(GNodeInfo({b1}, "nodeB2_dynamic", 1));
        auto* b3 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({b1}, "nodeB3_dynamic", 1));
        auto* b4 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({b2, b3}, "nodeB4_dynamic", 1));
        (void)b4;

        param->injected_ = true;
        CGRAPH_FUNCTION_END
    }
};


void tutorial_region_dynamic() {
    CStatus status;
    GPipelinePtr pipeline = GPipelineFactory::create();

    GElementPtr a = nullptr, c = nullptr;

    // 先注册一个空的region，内部节点在切面中动态写入
    auto* bRegion = pipeline->createGGroup<GRegion>({});
    if (nullptr == bRegion) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    GElementPtr bRegionElement = bRegion;
    status += pipeline->registerGElement<MyNode1>(&a, {}, "nodeA", 1);
    status += pipeline->registerGElement<GRegion>(&bRegionElement, {a}, "regionB", 1);
    status += pipeline->registerGElement<MyNode2>(&c, {bRegionElement}, "nodeC", 1);
    if (!status.isOK()) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    RegionDynamicBuildParam regionParam;
    regionParam.region_ = bRegion;
    bRegion->addGAspect<RegionDynamicBuildAspect, RegionDynamicBuildParam>(&regionParam);

    status += pipeline->init();
    if (!status.isOK()) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    CGRAPH_ECHO("[T30] first run, region nodes will be injected by aspect before region run");
    status += pipeline->run();
    if (!status.isOK()) {
        pipeline->destroy();
        GPipelineFactory::remove(pipeline);
        return;
    }

    CGRAPH_ECHO("[T30] second run, aspect is idempotent and no duplicate dynamic node is injected");
    status += pipeline->run();

    status += pipeline->destroy();
    CGRAPH_ECHO("tutorial region dynamic status is : [%d]", status.getCode());

    GPipelineFactory::remove(pipeline);
}

int main() {
    tutorial_region_dynamic();
    return 0;
}

/***************************
@Author: Copilot
@File: T30-RegionDynamic.cpp
@Time: 2026/3/2
@Desc: 演示Region(loop)包裹动态Region，触发重跑3次并验证每次重建动态节点
***************************/

#include "MyGNode/MyNode1.h"
#include "MyGNode/MyNode2.h"

#include <atomic>

using namespace CGraph;

static std::atomic<CSize> g_region_dynamic_round { 0 };
static std::atomic<CSize> g_region_name_version { 0 };

class RegionDynamicBuildParam : public GAspectParam {
public:
    CVoid clone(GPassedParam* param) override {
        auto* cur = dynamic_cast<RegionDynamicBuildParam*>(param);
        if (nullptr == cur) {
            return;
        }

        region_ = cur->region_;
    }

    GRegionPtr region_ { nullptr };
};


class RegionDynamicBuildAspect : public GAspect {
public:
    CStatus beginRun() override {
        CGRAPH_FUNCTION_BEGIN
        auto* param = this->getAParam<RegionDynamicBuildParam>();
        CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION(nullptr == param || nullptr == param->region_,
                                                "region dynamic build aspect param is invalid")

        auto round = g_region_dynamic_round.fetch_add(1, std::memory_order_release) + 1;
        auto version = g_region_name_version.fetch_add(1, std::memory_order_release) + 1;
        std::string parity = (round % 2 == 0) ? "even" : "odd";
        std::string suffix = "_round" + std::to_string(round)
                     + "_" + parity
                     + "_v" + std::to_string(version);

        // 在region真正执行前，动态创建并注册内部节点及依赖
        auto* b1 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({}, "nodeB1_dynamic" + suffix, 1));
        auto* b2 = param->region_->enqueueDynamicNode<MyNode2>(GNodeInfo({b1}, "nodeB2_dynamic" + suffix, 1));
        auto* b3 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({b1}, "nodeB3_dynamic" + suffix, 1));
        auto* b4 = param->region_->enqueueDynamicNode<MyNode1>(GNodeInfo({b2, b3}, "nodeB4_dynamic" + suffix, 1));
        (void)b4;
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

    // 再创建一个外层region，将内层region包裹起来，通过loop=3触发重跑
    auto* wrapRegion = pipeline->createGGroup<GRegion>({bRegion});
    if (nullptr == wrapRegion) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    GElementPtr wrapRegionElement = wrapRegion;
    status += pipeline->registerGElement<MyNode1>(&a, {}, "nodeA", 1);
    status += pipeline->registerGElement<GRegion>(&wrapRegionElement, {a}, "regionWrap", 3);
    status += pipeline->registerGElement<MyNode2>(&c, {wrapRegionElement}, "nodeC", 1);
    if (!status.isOK()) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    RegionDynamicBuildParam regionParam;
    regionParam.region_ = bRegion;
    bRegion->addGAspect<RegionDynamicBuildAspect, RegionDynamicBuildParam>(&regionParam);
    g_region_dynamic_round.store(0, std::memory_order_release);
    g_region_name_version.store(0, std::memory_order_release);

    status += pipeline->init();
    if (!status.isOK()) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    CGRAPH_ECHO("[T30] run once, inner region should rerun 3 times by outer region(loop=3)");
    status += pipeline->run();
    if (!status.isOK()) {
        pipeline->destroy();
        GPipelineFactory::remove(pipeline);
        return;
    }

    auto round = g_region_dynamic_round.load(std::memory_order_acquire);
    if (round == 3) {
        CGRAPH_ECHO("[T30][PASS] inner dynamic region rebuilt for 3 rounds");
    } else {
        CGRAPH_ECHO("[T30][FAIL] expect 3 rounds, actual [%zu]", round);
    }

    status += pipeline->destroy();
    CGRAPH_ECHO("tutorial region dynamic status is : [%d]", status.getCode());

    GPipelineFactory::remove(pipeline);
}

int main() {
    tutorial_region_dynamic();
    return 0;
}

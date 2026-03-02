/***************************
@Author: Copilot
@File: T31-RegionDynamicRegression.cpp
@Time: 2026/3/2
@Desc: GRegion 通过Aspect在run前动态注册回归用例（重复插入/非法依赖）
***************************/

#include "MyGNode/MyNode1.h"
#include "MyGNode/MyNode2.h"

using namespace CGraph;


class RegionInjectParam : public GAspectParam {
public:
    CVoid clone(GPassedParam* param) override {
        auto* cur = dynamic_cast<RegionInjectParam*>(param);
        if (nullptr == cur) {
            return;
        }

        region_ = cur->region_;
        tail_ = cur->tail_;
        outsider_ = cur->outsider_;
        mode_ = cur->mode_;
        injected_ = cur->injected_;
    }

    enum class Mode {
        DUPLICATE,
        INVALID_DEP
    };

    GRegionPtr region_ { nullptr };
    GElementPtr tail_ { nullptr };
    GElementPtr outsider_ { nullptr };
    Mode mode_ { Mode::DUPLICATE };
    CBool injected_ { false };
};


class RegionInjectAspect : public GAspect {
public:
    CStatus beginRun() override {
        CGRAPH_FUNCTION_BEGIN
        auto* param = this->getAParam<RegionInjectParam>();
        CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION(nullptr == param || nullptr == param->region_,
                                                "region inject aspect param is invalid")

        if (param->injected_) {
            return status;
        }

        if (param->mode_ == RegionInjectParam::Mode::DUPLICATE) {
            auto* dup = new(std::nothrow) MyNode1();
            CGRAPH_ASSERT_NOT_NULL(dup)
            status = param->region_->enqueueDynamicNode(dup, GNodeInfo({param->tail_}, "dup_dyn", 1));
            CGRAPH_FUNCTION_CHECK_STATUS
            status = param->region_->enqueueDynamicNode(dup, GNodeInfo({param->tail_}, "dup_dyn", 1));
            CGRAPH_FUNCTION_CHECK_STATUS
        } else {
            auto* bad = new(std::nothrow) MyNode1();
            CGRAPH_ASSERT_NOT_NULL(bad)
            status = param->region_->enqueueDynamicNode(bad, GNodeInfo({param->outsider_}, "bad_dep_dyn", 1));
            CGRAPH_FUNCTION_CHECK_STATUS
        }

        param->injected_ = true;
        CGRAPH_FUNCTION_END
    }
};


static GRegionPtr buildRegionPipeline(GPipelinePtr pipeline,
                                      GElementPtr& regionTail,
                                      GElementPtr& outerNode) {
    GElementPtr r1 = pipeline->createGNode<MyNode1>(GNodeInfo({}, "r1", 1));
    GElementPtr r2 = pipeline->createGNode<MyNode2>(GNodeInfo({r1}, "r2", 1));
    GElementPtr r3 = pipeline->createGNode<MyNode1>(GNodeInfo({r2}, "r3", 1));

    auto* region = pipeline->createGGroup<GRegion>({r1, r2, r3});
    if (nullptr == region) {
        return nullptr;
    }

    GElementPtr regionElement = region;
    CStatus status;
    status += pipeline->registerGElement<MyNode1>(&outerNode, {}, "outer", 1);
    status += pipeline->registerGElement<GRegion>(&regionElement, {outerNode}, "testRegion", 1);
    if (!status.isOK()) {
        return nullptr;
    }

    regionTail = r3;
    return region;
}

static void case_duplicate_node_enqueue() {
    CStatus status;
    GPipelinePtr pipeline = GPipelineFactory::create();
    GElementPtr tail = nullptr, outer = nullptr;
    auto* region = buildRegionPipeline(pipeline, tail, outer);
    if (nullptr == region) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    status += pipeline->init();
    if (!status.isOK()) {
        pipeline->destroy();
        GPipelineFactory::remove(pipeline);
        return;
    }

    RegionInjectParam param;
    param.region_ = region;
    param.tail_ = tail;
    param.mode_ = RegionInjectParam::Mode::DUPLICATE;
    region->addGAspect<RegionInjectAspect, RegionInjectParam>(&param);

    status = pipeline->run();
    if (status.isErr()) {
        CGRAPH_ECHO("[T31][PASS] duplicate dynamic node rejected, code=%d", status.getCode());
    } else {
        CGRAPH_ECHO("[T31][FAIL] duplicate dynamic node should be rejected");
    }

    pipeline->destroy();
    GPipelineFactory::remove(pipeline);
}

static void case_invalid_dependency_enqueue() {
    CStatus status;
    GPipelinePtr pipeline = GPipelineFactory::create();
    GElementPtr tail = nullptr, outer = nullptr;
    auto* region = buildRegionPipeline(pipeline, tail, outer);
    if (nullptr == region) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    // 外部节点，不属于 region，作为动态节点依赖应被拒绝
    GElementPtr outsider = pipeline->createGNode<MyNode1>(GNodeInfo({}, "outsider", 1));

    status += pipeline->init();
    if (!status.isOK()) {
        GPipelineFactory::remove(pipeline);
        return;
    }

    RegionInjectParam param;
    param.region_ = region;
    param.tail_ = tail;
    param.outsider_ = outsider;
    param.mode_ = RegionInjectParam::Mode::INVALID_DEP;
    region->addGAspect<RegionInjectAspect, RegionInjectParam>(&param);

    status = pipeline->run();
    if (status.isErr()) {
        CGRAPH_ECHO("[T31][PASS] invalid dependency rejected, code=%d", status.getCode());
    } else {
        CGRAPH_ECHO("[T31][FAIL] invalid dependency should be rejected");
    }

    pipeline->destroy();
    GPipelineFactory::remove(pipeline);
}

int main() {
    case_duplicate_node_enqueue();
    case_invalid_dependency_enqueue();
    return 0;
}

/***************************
@Author: Chunel
@Contact: chunel@foxmail.com
@File: GRegion.cpp
@Time: 2021/6/1 10:14 下午
@Desc: 
***************************/

#include "GRegion.h"
#include "../../_GOptimizer/GOptimizerInclude.h"

CGRAPH_NAMESPACE_BEGIN

GRegion::GRegion() : GGroup() {
    is_init_ = false;
    manager_ = CGRAPH_SAFE_MALLOC_COBJECT(GElementManager)
    element_type_ = GElementType::REGION;
    session_ = URandom<>::generateSession(CGRAPH_STR_REGION);
}


GRegion::~GRegion() {
    CGRAPH_DELETE_PTR(manager_)
}


CStatus GRegion::enqueueDynamicNode(GElementPtr node, const GNodeInfo& info) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_NOT_NULL(node)
    CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION(!node->isGNode(),
                                            "enqueue dynamic element support GNode only")
    CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION((nullptr != node->belong_ && node->belong_ != this),
                                            "enqueue dynamic element belong to other group")
    CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION((node->belong_ == this && node->is_init_),
                                            "enqueue dynamic element duplicate in region")

    {
        CGRAPH_LOCK_GUARD lock(pending_dynamic_lock_);
        auto isDup = std::any_of(pending_dynamic_nodes_.begin(), pending_dynamic_nodes_.end(),
                                 [node](const GDynamicNodeInfo& cur) {
                                     return cur.node_ == node;
                                 });
        CGRAPH_RETURN_ERROR_STATUS_BY_CONDITION(isDup,
                                                "enqueue dynamic element duplicate in pending queue")

        pending_dynamic_nodes_.emplace_back();
        pending_dynamic_nodes_.back().node_ = node;
        pending_dynamic_nodes_.back().info_ = info;
    }
    has_pending_dynamic_node_.store(true, std::memory_order_release);
    CGRAPH_FUNCTION_END
}


CStatus GRegion::init() {
    CGRAPH_FUNCTION_BEGIN
    // 在这里将初始化所有的节点信息，并且实现分析，联通等功能
    CGRAPH_ASSERT_NOT_NULL(manager_)

    status = this->manager_->init();
    CGRAPH_FUNCTION_CHECK_STATUS

    is_init_ = true;
    CGRAPH_FUNCTION_END
}


CStatus GRegion::destroy() {
    CGRAPH_FUNCTION_BEGIN
    status += teardownDynamicNodes();
    CGRAPH_FUNCTION_CHECK_STATUS

    status = manager_->destroy();
    CGRAPH_FUNCTION_CHECK_STATUS

    {
        std::vector<GDynamicNodeInfo> pendingNodes;
        {
            CGRAPH_LOCK_GUARD lock(pending_dynamic_lock_);
            pendingNodes.swap(pending_dynamic_nodes_);
            has_pending_dynamic_node_.store(false, std::memory_order_release);
        }

        for (auto& pending : pendingNodes) {
            if (pending.node_) {
                auto* node = pending.node_;
                pending.node_ = nullptr;
                delete node;
            }
        }
    }

    is_init_ = false;
    CGRAPH_FUNCTION_END
}


CStatus GRegion::run() {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_INIT(true)
    CGRAPH_ASSERT_NOT_NULL(manager_)

    status = applyDynamicNodes();
    CGRAPH_FUNCTION_CHECK_STATUS

    status = manager_->run();

    auto runStatus = status;
    status += teardownDynamicNodes();
    CGRAPH_FUNCTION_CHECK_STATUS
    status = runStatus;

    // 特殊处理的重试逻辑，当收到status  == RETRY || SKIP的时候，重新执行一次
    if (status.getCode() == STATUS_TASK_REGION_RETRY || status.getCode() == STATUS_TASK_REGION_SKIP) {
        // 重试或者跳过，均表示当前region执行成功 通过isHold来控制后续流程
        return CStatus();    
    }
    CGRAPH_FUNCTION_END
}


CStatus GRegion::addElementEx(GElementPtr element) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_NOT_NULL(element, manager_)

    manager_->manager_elements_.emplace(element);
    CGRAPH_FUNCTION_END
}


GElementPtr GRegion::setThreadPoolEx(UThreadPoolPtr ptr) {
    CGRAPH_ASSERT_NOT_NULL_THROW_ERROR(manager_, ptr)
    manager_->setThreadPool(ptr);
    return this;
}


GRegionPtr GRegion::setGEngineType(GEngineType type) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)
    CGRAPH_ASSERT_NOT_NULL_THROW_ERROR(manager_)

    this->manager_->setEngineType(type);
    return this;
}


CVoid GRegion::dump(std::ostream& oss) {
    dumpElement(oss);
    dumpGroupLabelBegin(oss);
    oss << 'p' << this << "[shape=point height=0];\n";
    oss << "color=blue;\n";

    for (const auto& element : manager_->manager_elements_) {
        element->dump(oss);
    }

    dumpGroupLabelEnd(oss);

    for (const auto& element : run_before_) {
        dumpEdge(oss, this, element);
    }
}


CBool GRegion::isSerializable() const {
    if (nullptr == manager_) {
        return false;
    }

    return manager_->checkSerializable();
}


CBool GRegion::isSeparate(GElementCPtr a, GElementCPtr b) const {
    return GSeparateOptimizer::checkSeparate(manager_->manager_elements_, a, b);
}


CStatus GRegion::applyDynamicNodes() {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_NOT_NULL(manager_)

    if (!has_pending_dynamic_node_.load(std::memory_order_acquire)) {
        return status;
    }

    std::vector<GDynamicNodeInfo> pendingNodes;
    {
        CGRAPH_LOCK_GUARD lock(pending_dynamic_lock_);
        if (pending_dynamic_nodes_.empty()) {
            has_pending_dynamic_node_.store(false, std::memory_order_release);
            return status;
        }
        pendingNodes.swap(pending_dynamic_nodes_);
        has_pending_dynamic_node_.store(false, std::memory_order_release);
    }

    for (CSize idx = 0; idx < pendingNodes.size(); idx++) {
        const auto& pendingNode = pendingNodes[idx];
        auto* node = pendingNode.node_;
        CGRAPH_ASSERT_NOT_NULL(node)

        auto* oldBelong = node->belong_;

        // 依赖关系校验会检查 belong_ 一致性，先绑定 region 再写依赖
        node->belong_ = this;

        status = node->addElementInfo(pendingNode.info_.dependence_, pendingNode.info_.name_, pendingNode.info_.loop_);
        if (status.isErr()) {
            node->belong_ = oldBelong;
        }
        CGRAPH_FUNCTION_CHECK_STATUS

        status = node->addManagers(param_manager_, event_manager_, stage_manager_);
        if (status.isErr()) {
            for (auto* dependence : node->dependence_) {
                dependence->run_before_.remove(node);
            }
            node->dependence_.clear();
            node->left_depend_.store(0, std::memory_order_release);
            node->belong_ = oldBelong;
        }
        CGRAPH_FUNCTION_CHECK_STATUS

        // 动态节点上的aspect需要在INIT前补齐belong/manager上下文
        node->updateAspectInfo();

        status = node->fatProcessor(CFunctionType::INIT);
        if (status.isErr()) {
            for (auto* dependence : node->dependence_) {
                dependence->run_before_.remove(node);
            }
            node->dependence_.clear();
            node->left_depend_.store(0, std::memory_order_release);
            node->belong_ = oldBelong;

            {
                CGRAPH_LOCK_GUARD lock(pending_dynamic_lock_);
                for (CSize remain = idx; remain < pendingNodes.size(); remain++) {
                    pending_dynamic_nodes_.emplace_back();
                    pending_dynamic_nodes_.back().node_ = pendingNodes[remain].node_;
                    pending_dynamic_nodes_.back().info_ = pendingNodes[remain].info_;
                }
                has_pending_dynamic_node_.store(true, std::memory_order_release);
            }
        }
        CGRAPH_FUNCTION_CHECK_STATUS

        node->is_init_ = true;

        group_elements_arr_.emplace_back(node);
        manager_->manager_elements_.emplace(node);
        dynamic_nodes_.emplace(node);
    }

    // region 的执行引擎会缓存拓扑，新增节点后需要重建
    status = manager_->initEngine();
    CGRAPH_FUNCTION_END
}


CStatus GRegion::teardownDynamicNodes() {
    CGRAPH_FUNCTION_BEGIN
    if (dynamic_nodes_.empty()) {
        return status;
    }

    std::vector<GElementPtr> dynamicNodes(dynamic_nodes_.begin(), dynamic_nodes_.end());
    for (auto* node : dynamicNodes) {
        CGRAPH_ASSERT_NOT_NULL(node)

        for (auto* dependence : node->dependence_) {
            if (dependence) {
                dependence->run_before_.remove(node);
            }
        }

        for (auto* successor : node->run_before_) {
            if (successor) {
                successor->dependence_.remove(node);
                successor->left_depend_.store(successor->dependence_.size(), std::memory_order_release);
            }
        }

        node->dependence_.clear();
        node->run_before_.clear();
        node->left_depend_.store(0, std::memory_order_release);

        if (node->is_init_) {
            status += node->fatProcessor(CFunctionType::DESTROY);
            CGRAPH_FUNCTION_CHECK_STATUS
            node->is_init_ = false;
        }

        manager_->manager_elements_.erase(node);
        group_elements_arr_.erase(std::remove(group_elements_arr_.begin(), group_elements_arr_.end(), node),
                                  group_elements_arr_.end());
        delete node;
    }

    dynamic_nodes_.clear();
    status = manager_->initEngine();
    CGRAPH_FUNCTION_END
}


CSize GRegion::trim() {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)
    CSize result = 0;
    if (manager_) {
        result = GTrimOptimizer::trim(manager_->manager_elements_);
    }
    return result;
}

CGRAPH_NAMESPACE_END
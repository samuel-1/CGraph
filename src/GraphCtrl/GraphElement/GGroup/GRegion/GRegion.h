/***************************
@Author: Chunel
@Contact: chunel@foxmail.com
@File: GRegion.h
@Time: 2021/6/1 10:14 下午
@Desc: 实现多个element，根据依赖关系执行的功能
***************************/


#ifndef CGRAPH_GREGION_H
#define CGRAPH_GREGION_H

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "../GGroup.h"
#include "../../GElementManager.h"
#include "../../GNode/GNode.h"
#include "../../GNode/GNodeDefine.h"

static const int STATUS_TASK_REGION_RETRY = -500;                      /** 重试流程重试当前Region返回值 */
static const int STATUS_TASK_REGION_SKIP = -501;                       /** 重试流程跳过当前Region返回值 */
static const int STATUS_TASK_PARAM_SKIP = -502;                        /** 参数异常，跳过当前Region返回值 */

CGRAPH_NAMESPACE_BEGIN


class GRegion : public GGroup {
public:
    /**
     * 运行时动态添加一个 node，默认在下次 run() 时生效
     * @param node
     * @param info
     * @return
     */
    CStatus enqueueDynamicNode(GElementPtr node, const GNodeInfo& info);

    /**
     * 运行时动态创建并添加一个 node，默认在下次 run() 时生效
     * @tparam TNode
     * @tparam Args
     * @param info
     * @param args
     * @return
     */
    template<typename TNode, typename ...Args,
            c_enable_if_t<std::is_base_of<GNode, TNode>::value, int> = 0>
    TNode* enqueueDynamicNode(const GNodeInfo& info, Args&&... args) {
        auto* node = new(std::nothrow) TNode(std::forward<Args &&>(args)...);
        CGRAPH_ASSERT_NOT_NULL_THROW_ERROR(node)
        auto status = enqueueDynamicNode(node, info);
        CGRAPH_THROW_EXCEPTION_BY_STATUS(status)
        return node;
    }

    /**
     * 设置EngineType信息
     * @param type
     * @return
     */
    GRegion* setGEngineType(GEngineType type);

    /**
     * 修剪冗余的连边信息
     * @return
     */
    CSize trim();

protected:
    explicit GRegion();
    ~GRegion() override;

    CStatus init() final;
    CStatus run() final;
    CStatus destroy() final;

private:
    struct GDynamicNodeInfo {
        GElementPtr node_ { nullptr };
        GNodeInfo info_ { GElementPtrSet{}, CGRAPH_EMPTY, CGRAPH_DEFAULT_LOOP_TIMES };
    };

    CVoid dump(std::ostream& oss) final;

    CBool isSerializable() const final;

    CStatus addElementEx(GElementPtr element) final;

    GElementPtr setThreadPoolEx(UThreadPoolPtr ptr) final;

    CBool isSeparate(GElementCPtr a, GElementCPtr b) const final;

    /**
     * 将排队中的动态节点写入 region，并刷新内部执行引擎
     * @return
     */
    CStatus applyDynamicNodes();

    /**
     * 每次run结束后，销毁本轮动态注入的节点
     * @return
     */
    CStatus teardownDynamicNodes();

private:
    GElementManagerPtr manager_ = nullptr;    // region 内部通过 manager来管理其中的 element 信息
    std::vector<GDynamicNodeInfo> pending_dynamic_nodes_ {};    // 运行中待写入的节点
    std::mutex pending_dynamic_lock_ {};
    std::atomic<CBool> has_pending_dynamic_node_ { false };
    std::unordered_set<GElementPtr> dynamic_nodes_ {};          // 当前已生效的动态节点

    CGRAPH_NO_ALLOWED_COPY(GRegion)

    friend class GPipeline;
    friend class CAllocator;
    friend class GStorageFactory;
    friend class GTrimOptimizer;
};

using GRegionPtr = GRegion *;

CGRAPH_NAMESPACE_END

#endif //CGRAPH_GREGION_H

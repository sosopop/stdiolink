/// @file js_task_scheduler.h
/// @brief 异步任务调度器，管理 JS 端 pending Task 的 resolve/reject

#pragma once

#include <QVector>

#include <quickjs.h>
#include "stdiolink/host/task.h"

/// @brief 异步任务调度器
///
/// 管理 JS 端通过 Promise 发起的异步 Task。当 JS 调用异步命令时，
/// 调度器保存对应的 resolve/reject 回调；通过 poll() 轮询 Task 完成状态，
/// 自动调用相应的 resolve 或 reject 来兑现 Promise。
/// 不可拷贝，确保回调引用的唯一所有权。
class JsTaskScheduler {
public:
    /// @brief 构造函数
    /// @param ctx QuickJS 上下文，用于操作 JSValue
    explicit JsTaskScheduler(JSContext* ctx);

    /// @brief 析构函数，释放所有未完成 Task 的 resolve/reject 引用
    ~JsTaskScheduler();

    JsTaskScheduler(const JsTaskScheduler&) = delete;
    JsTaskScheduler& operator=(const JsTaskScheduler&) = delete;

    /// @brief 添加一个待处理的异步 Task
    /// @param task stdiolink Task 对象
    /// @param resolve Promise 的 resolve 回调（所有权转移给调度器）
    /// @param reject Promise 的 reject 回调（所有权转移给调度器）
    void addTask(const stdiolink::Task& task, JSValue resolve, JSValue reject);

    /// @brief 轮询所有 pending Task，对已完成的调用 resolve/reject
    /// @param timeoutMs 单次轮询的超时时间（毫秒），默认 50ms
    /// @return 如果仍有未完成的 Task 返回 true
    bool poll(int timeoutMs = 50);

    /// @brief 检查是否有未完成的 Task
    /// @return 有 pending Task 返回 true
    bool hasPending() const;

    /// @brief 将调度器实例注册到 JS 全局对象
    /// @param ctx QuickJS 上下文
    /// @param scheduler 调度器实例指针
    static void installGlobal(JSContext* ctx, JsTaskScheduler* scheduler);

private:
    /// @brief 待处理任务条目，关联 Task 与其 Promise 回调
    struct PendingTask {
        stdiolink::Task task;              ///< stdiolink Task 对象
        JSValue resolve = JS_UNDEFINED;    ///< Promise resolve 回调
        JSValue reject = JS_UNDEFINED;     ///< Promise reject 回调
    };

    /// @brief 完成指定索引的 Task，调用 resolve 或 reject
    /// @param index pending 列表中的索引
    /// @param value 传递给回调的值
    /// @param useReject 为 true 时调用 reject，否则调用 resolve
    void settleTask(int index, JSValue value, bool useReject);

    JSContext* m_ctx = nullptr;        ///< QuickJS 上下文
    QVector<PendingTask> m_pending;    ///< 待处理任务列表
};

/// @file js_wait_any_scheduler.h
/// @brief waitAny 异步调度器，管理 JS 端多 Task 多路监听

#pragma once

#include <QElapsedTimer>
#include <QVector>

#include <quickjs.h>
#include "stdiolink/host/task.h"

/// @brief waitAny 异步调度器
///
/// 管理 JS 端通过 waitAny() 发起的监听组。每个监听组包含多个 Task，
/// poll() 每轮至多兑现一个监听组，resolve 值为 { taskIndex, msg }。
class WaitAnyScheduler {
public:
    explicit WaitAnyScheduler(JSContext* ctx);
    ~WaitAnyScheduler();

    WaitAnyScheduler(const WaitAnyScheduler&) = delete;
    WaitAnyScheduler& operator=(const WaitAnyScheduler&) = delete;

    /// @brief 添加监听组
    /// @param tasks Task 数组（拷贝，共享 TaskState）
    /// @param timeoutMs 超时时间，-1 表示无限等待
    /// @param resolve Promise resolve 回调（所有权转移）
    /// @param reject Promise reject 回调（所有权转移）
    void addGroup(const QVector<stdiolink::Task>& tasks, int timeoutMs,
                  JSValue resolve, JSValue reject);

    /// @brief 驱动一轮调度
    /// @param timeoutMs 调度超时，默认 50ms
    /// @return 仍有 pending 组返回 true
    bool poll(int timeoutMs = 50);

    /// @brief 是否存在 pending 监听组
    bool hasPending() const;

    /// @brief 安装 __waitAny 全局函数
    static void installGlobal(JSContext* ctx, WaitAnyScheduler* scheduler);

private:
    struct PendingGroup {
        QVector<stdiolink::Task> tasks;
        int timeoutMs = -1;
        QElapsedTimer elapsed;
        JSValue resolve = JS_UNDEFINED;
        JSValue reject = JS_UNDEFINED;
    };

    void settleGroup(int index, JSValue value, bool useReject);

    JSContext* m_ctx = nullptr;
    QVector<PendingGroup> m_pending;
};

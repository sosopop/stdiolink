#pragma once

#include <QVector>

#include "quickjs.h"
#include "stdiolink/host/task.h"

class JsTaskScheduler {
public:
    explicit JsTaskScheduler(JSContext* ctx);
    ~JsTaskScheduler();

    JsTaskScheduler(const JsTaskScheduler&) = delete;
    JsTaskScheduler& operator=(const JsTaskScheduler&) = delete;

    void addTask(const stdiolink::Task& task, JSValue resolve, JSValue reject);
    bool poll(int timeoutMs = 50);
    bool hasPending() const;

    static void installGlobal(JSContext* ctx, JsTaskScheduler* scheduler);

private:
    struct PendingTask {
        stdiolink::Task task;
        JSValue resolve = JS_UNDEFINED;
        JSValue reject = JS_UNDEFINED;
    };

    void settleTask(int index, JSValue value, bool useReject);

    JSContext* m_ctx = nullptr;
    QVector<PendingTask> m_pending;
};


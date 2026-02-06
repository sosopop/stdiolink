#include "js_task_scheduler.h"

#include <QHash>

#include "js_task.h"
#include "utils/js_convert.h"
#include "stdiolink/host/wait_any.h"

namespace {

QHash<quintptr, JsTaskScheduler*> s_schedulers;

JSValue messageToJs(JSContext* ctx, const stdiolink::Message& msg) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewString(ctx, msg.status.toUtf8().constData()));
    JS_SetPropertyStr(ctx, obj, "code", JS_NewInt32(ctx, msg.code));
    JS_SetPropertyStr(ctx, obj, "data", qjsonToJsValue(ctx, msg.payload));
    return obj;
}

JSValue jsScheduleTask(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsTaskScheduler* scheduler = s_schedulers.value(reinterpret_cast<quintptr>(ctx), nullptr);
    if (!scheduler) {
        return JS_ThrowInternalError(ctx, "__scheduleTask is not installed");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "__scheduleTask(task): missing task");
    }

    stdiolink::Task task;
    if (!JsTaskBinding::toTask(ctx, argv[0], task)) {
        return JS_ThrowTypeError(ctx, "__scheduleTask(task): invalid task");
    }

    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        return promise;
    }

    scheduler->addTask(task, resolvingFuncs[0], resolvingFuncs[1]);
    return promise;
}

} // namespace

JsTaskScheduler::JsTaskScheduler(JSContext* ctx) : m_ctx(ctx) {}

JsTaskScheduler::~JsTaskScheduler() {
    s_schedulers.remove(reinterpret_cast<quintptr>(m_ctx));
    if (!m_ctx) {
        return;
    }

    for (const PendingTask& item : m_pending) {
        JS_FreeValue(m_ctx, item.resolve);
        JS_FreeValue(m_ctx, item.reject);
    }
    m_pending.clear();
}

void JsTaskScheduler::addTask(const stdiolink::Task& task, JSValue resolve, JSValue reject) {
    PendingTask item;
    item.task = task;
    item.resolve = resolve;
    item.reject = reject;
    m_pending.push_back(item);
}

bool JsTaskScheduler::poll(int timeoutMs) {
    if (!m_ctx || m_pending.isEmpty()) {
        return false;
    }

    QVector<stdiolink::Task> tasks;
    tasks.reserve(m_pending.size());
    for (const PendingTask& item : m_pending) {
        tasks.push_back(item.task);
    }

    stdiolink::AnyItem anyItem;
    const bool gotMessage = stdiolink::waitAnyNext(tasks, anyItem, timeoutMs);
    if (gotMessage && anyItem.taskIndex >= 0 && anyItem.taskIndex < m_pending.size()) {
        const int index = anyItem.taskIndex;
        if (anyItem.msg.status == "done" || anyItem.msg.status == "error") {
            JSValue msg = messageToJs(m_ctx, anyItem.msg);
            settleTask(index, msg, false);
        }
    } else {
        // No message in this round; finish any task that is already done.
        for (int i = m_pending.size() - 1; i >= 0; --i) {
            if (m_pending[i].task.isDone() && !m_pending[i].task.hasQueued()) {
                settleTask(i, JS_NULL, false);
            }
        }
    }

    return !m_pending.isEmpty();
}

bool JsTaskScheduler::hasPending() const {
    return !m_pending.isEmpty();
}

void JsTaskScheduler::installGlobal(JSContext* ctx, JsTaskScheduler* scheduler) {
    if (!ctx || !scheduler) {
        return;
    }

    s_schedulers.insert(reinterpret_cast<quintptr>(ctx), scheduler);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__scheduleTask", JS_NewCFunction(ctx, jsScheduleTask, "__scheduleTask", 1));
    JS_FreeValue(ctx, global);
}

void JsTaskScheduler::settleTask(int index, JSValue value, bool useReject) {
    PendingTask item = m_pending[index];
    m_pending.removeAt(index);

    JSValue func = useReject ? item.reject : item.resolve;
    JSValue args[1] = {value};
    JSValue callRet = JS_Call(m_ctx, func, JS_UNDEFINED, 1, args);
    JS_FreeValue(m_ctx, callRet);
    JS_FreeValue(m_ctx, value);

    JS_FreeValue(m_ctx, item.resolve);
    JS_FreeValue(m_ctx, item.reject);
}

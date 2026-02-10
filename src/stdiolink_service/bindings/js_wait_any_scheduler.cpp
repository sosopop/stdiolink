#include "js_wait_any_scheduler.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>

#include "js_task.h"
#include "utils/js_convert.h"
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"

namespace {

struct TaskRef {
    int groupIndex = -1;
    int taskIndex = -1;
};

QHash<quintptr, WaitAnyScheduler*> s_schedulers;

constexpr int kDriverExitedCode = 1001;
const char* kDriverExitedMessage = "driver process exited before terminal response";

JSValue messageToJs(JSContext* ctx, const stdiolink::Message& msg) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewString(ctx, msg.status.toUtf8().constData()));
    JS_SetPropertyStr(ctx, obj, "code", JS_NewInt32(ctx, msg.code));
    JS_SetPropertyStr(ctx, obj, "data", qjsonToJsValue(ctx, msg.payload));
    return obj;
}

JSValue waitAnyResultToJs(JSContext* ctx, int taskIndex, const stdiolink::Message& msg) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "taskIndex", JS_NewInt32(ctx, taskIndex));
    JS_SetPropertyStr(ctx, obj, "msg", messageToJs(ctx, msg));
    return obj;
}

void settleImmediate(JSContext* ctx, JSValue resolve, JSValue reject, JSValue value, bool useReject) {
    JSValue fn = useReject ? reject : resolve;
    JSValue args[1] = {value};
    JSValue callRet = JS_Call(ctx, fn, JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, callRet);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, resolve);
    JS_FreeValue(ctx, reject);
}

void resolveImmediateNull(JSContext* ctx, JSValue resolve, JSValue reject) {
    settleImmediate(ctx, resolve, reject, JS_NULL, false);
}

void rejectImmediate(JSContext* ctx, JSValue resolve, JSValue reject, const char* message) {
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, message));
    settleImmediate(ctx, resolve, reject, err, true);
}

JSValue jsWaitAny(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    WaitAnyScheduler* scheduler = s_schedulers.value(reinterpret_cast<quintptr>(ctx), nullptr);
    if (!scheduler) {
        return JS_ThrowInternalError(ctx, "__waitAny is not installed");
    }
    if (argc < 1 || !JS_IsArray(argv[0])) {
        return JS_ThrowTypeError(ctx, "__waitAny(tasks, timeoutMs?): tasks must be an array");
    }

    JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    QVector<stdiolink::Task> tasks;
    tasks.reserve(static_cast<int>(len));
    for (uint32_t i = 0; i < len; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        stdiolink::Task task;
        if (!JsTaskBinding::toTask(ctx, item, task)) {
            JS_FreeValue(ctx, item);
            return JS_ThrowTypeError(ctx, "__waitAny: array element %u is not a Task", i);
        }
        tasks.push_back(task);
        JS_FreeValue(ctx, item);
    }

    int timeoutMs = -1;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (JS_ToInt32(ctx, &timeoutMs, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
        if (timeoutMs < -1) {
            return JS_ThrowRangeError(ctx, "__waitAny: timeoutMs must be >= -1");
        }
    }

    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        return promise;
    }

    scheduler->addGroup(tasks, timeoutMs, resolvingFuncs[0], resolvingFuncs[1]);
    return promise;
}

} // namespace

WaitAnyScheduler::WaitAnyScheduler(JSContext* ctx) : m_ctx(ctx) {}

WaitAnyScheduler::~WaitAnyScheduler() {
    s_schedulers.remove(reinterpret_cast<quintptr>(m_ctx));
    if (!m_ctx) {
        return;
    }

    for (const PendingGroup& item : m_pending) {
        JSValue err = JS_NewError(m_ctx);
        JS_SetPropertyStr(m_ctx, err, "message", JS_NewString(m_ctx, "WaitAnyScheduler destroyed"));
        JSValue args[1] = {err};
        JSValue callRet = JS_Call(m_ctx, item.reject, JS_UNDEFINED, 1, args);
        JS_FreeValue(m_ctx, callRet);
        JS_FreeValue(m_ctx, err);
        JS_FreeValue(m_ctx, item.resolve);
        JS_FreeValue(m_ctx, item.reject);
    }
    m_pending.clear();
}

void WaitAnyScheduler::addGroup(const QVector<stdiolink::Task>& tasks, int timeoutMs,
                                JSValue resolve, JSValue reject) {
    if (!m_ctx) {
        return;
    }
    if (tasks.isEmpty()) {
        resolveImmediateNull(m_ctx, resolve, reject);
        return;
    }

    QSet<const stdiolink::TaskState*> currentStates;
    for (const stdiolink::Task& task : tasks) {
        if (!task.isValid() || task.stateId() == nullptr) {
            rejectImmediate(m_ctx, resolve, reject, "waitAny: all items must be valid Task");
            return;
        }
        const auto* stateId = task.stateId();
        if (currentStates.contains(stateId)) {
            rejectImmediate(m_ctx, resolve, reject, "waitAny: duplicate Task in tasks array");
            return;
        }
        currentStates.insert(stateId);
    }

    for (const PendingGroup& group : m_pending) {
        for (const stdiolink::Task& existing : group.tasks) {
            if (currentStates.contains(existing.stateId())) {
                rejectImmediate(
                    m_ctx, resolve, reject,
                    "waitAny conflict: the same Task is already in a pending waitAny group");
                return;
            }
        }
    }

    PendingGroup item;
    item.tasks = tasks;
    item.timeoutMs = timeoutMs;
    item.elapsed.start();
    item.resolve = resolve;
    item.reject = reject;
    m_pending.push_back(item);
}

bool WaitAnyScheduler::poll(int timeoutMs) {
    if (!m_ctx || m_pending.isEmpty()) {
        return false;
    }

    for (int i = m_pending.size() - 1; i >= 0; --i) {
        const PendingGroup& group = m_pending[i];
        if (group.timeoutMs >= 0 && group.elapsed.hasExpired(group.timeoutMs)) {
            settleGroup(i, JS_NULL, false);
        }
    }
    if (m_pending.isEmpty()) {
        return false;
    }

    for (int i = m_pending.size() - 1; i >= 0; --i) {
        PendingGroup& group = m_pending[i];
        bool settled = false;
        for (int taskIndex = 0; taskIndex < group.tasks.size(); ++taskIndex) {
            stdiolink::Task& task = group.tasks[taskIndex];
            if (!task.isValid() || task.isDone()) {
                continue;
            }
            stdiolink::Driver* owner = task.owner();
            if (!owner) {
                continue;
            }
            if (!owner->isRunning()) {
                task.forceTerminal(kDriverExitedCode, QString::fromUtf8(kDriverExitedMessage));
                stdiolink::Message msg;
                msg.status = "error";
                msg.code = kDriverExitedCode;
                msg.payload = QJsonObject{{"message", QString::fromUtf8(kDriverExitedMessage)}};
                settleGroup(i, waitAnyResultToJs(m_ctx, taskIndex, msg), false);
                settled = true;
                break;
            }
        }
        if (settled) {
            continue;
        }
    }
    if (m_pending.isEmpty()) {
        return false;
    }

    QVector<stdiolink::Task> allTasks;
    QVector<TaskRef> refs;
    for (int groupIndex = 0; groupIndex < m_pending.size(); ++groupIndex) {
        const PendingGroup& group = m_pending[groupIndex];
        for (int taskIndex = 0; taskIndex < group.tasks.size(); ++taskIndex) {
            allTasks.push_back(group.tasks[taskIndex]);
            refs.push_back(TaskRef{groupIndex, taskIndex});
        }
    }

    stdiolink::AnyItem anyItem;
    const bool gotMessage = stdiolink::waitAnyNext(allTasks, anyItem, timeoutMs);
    if (gotMessage && anyItem.taskIndex >= 0 && anyItem.taskIndex < refs.size()) {
        const TaskRef ref = refs[anyItem.taskIndex];
        if (ref.groupIndex >= 0 && ref.groupIndex < m_pending.size()) {
            settleGroup(ref.groupIndex,
                        waitAnyResultToJs(m_ctx, ref.taskIndex, anyItem.msg),
                        false);
        }
    } else {
        for (int i = m_pending.size() - 1; i >= 0; --i) {
            bool allDone = true;
            for (const stdiolink::Task& task : m_pending[i].tasks) {
                if (task.isValid() && !task.isDone()) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) {
                settleGroup(i, JS_NULL, false);
            }
        }
    }

    return !m_pending.isEmpty();
}

bool WaitAnyScheduler::hasPending() const {
    return !m_pending.isEmpty();
}

void WaitAnyScheduler::installGlobal(JSContext* ctx, WaitAnyScheduler* scheduler) {
    if (!ctx || !scheduler) {
        return;
    }

    s_schedulers.insert(reinterpret_cast<quintptr>(ctx), scheduler);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__waitAny", JS_NewCFunction(ctx, jsWaitAny, "__waitAny", 2));
    JS_FreeValue(ctx, global);
}

void WaitAnyScheduler::settleGroup(int index, JSValue value, bool useReject) {
    PendingGroup item = m_pending[index];
    m_pending.removeAt(index);

    JSValue func = useReject ? item.reject : item.resolve;
    JSValue args[1] = {value};
    JSValue callRet = JS_Call(m_ctx, func, JS_UNDEFINED, 1, args);
    JS_FreeValue(m_ctx, callRet);
    JS_FreeValue(m_ctx, value);

    JS_FreeValue(m_ctx, item.resolve);
    JS_FreeValue(m_ctx, item.reject);
}

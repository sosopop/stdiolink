#include "js_task.h"

#include <QHash>

#include "utils/js_convert.h"

namespace {

struct JsTaskOpaque {
    stdiolink::Task task;
};

QHash<quintptr, JSClassID> s_taskClassIds;

JSClassID classIdForRuntime(JSRuntime* rt) {
    return s_taskClassIds.value(reinterpret_cast<quintptr>(rt), 0);
}

void jsTaskFinalizer(JSRuntime* rt, JSValueConst val);
JSValue jsTaskTryNext(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsTaskWaitNext(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsTaskGetDone(JSContext* ctx, JSValueConst thisVal);
JSValue jsTaskGetExitCode(JSContext* ctx, JSValueConst thisVal);
JSValue jsTaskGetErrorText(JSContext* ctx, JSValueConst thisVal);
JSValue jsTaskGetFinalPayload(JSContext* ctx, JSValueConst thisVal);

const JSCFunctionListEntry kTaskProtoFuncs[] = {
    JS_CFUNC_DEF("tryNext", 0, jsTaskTryNext),
    JS_CFUNC_DEF("waitNext", 1, jsTaskWaitNext),
    JS_CGETSET_DEF("done", jsTaskGetDone, nullptr),
    JS_CGETSET_DEF("exitCode", jsTaskGetExitCode, nullptr),
    JS_CGETSET_DEF("errorText", jsTaskGetErrorText, nullptr),
    JS_CGETSET_DEF("finalPayload", jsTaskGetFinalPayload, nullptr),
};

JSClassID ensureTaskClass(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    const JSClassID existing = classIdForRuntime(rt);
    if (existing != 0) {
        return existing;
    }

    JSClassID classId = 0;
    JS_NewClassID(rt, &classId);

    JSClassDef clsDef{};
    clsDef.class_name = "Task";
    clsDef.finalizer = jsTaskFinalizer;
    if (JS_NewClass(rt, classId, &clsDef) < 0) {
        return 0;
    }

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, kTaskProtoFuncs,
                               static_cast<int>(sizeof(kTaskProtoFuncs) / sizeof(kTaskProtoFuncs[0])));
    JS_SetClassProto(ctx, classId, proto);

    s_taskClassIds.insert(reinterpret_cast<quintptr>(rt), classId);
    return classId;
}

JsTaskOpaque* getTaskOpaque(JSContext* ctx, JSValueConst thisVal) {
    const JSClassID classId = classIdForRuntime(JS_GetRuntime(ctx));
    if (classId == 0) {
        return nullptr;
    }
    return static_cast<JsTaskOpaque*>(JS_GetOpaque2(ctx, thisVal, classId));
}

JSValue taskMessageToJs(JSContext* ctx, const stdiolink::Message& msg) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewString(ctx, msg.status.toUtf8().constData()));
    JS_SetPropertyStr(ctx, obj, "code", JS_NewInt32(ctx, msg.code));
    JS_SetPropertyStr(ctx, obj, "data", qjsonToJsValue(ctx, msg.payload));
    return obj;
}

void jsTaskFinalizer(JSRuntime* rt, JSValueConst val) {
    const JSClassID classId = classIdForRuntime(rt);
    if (classId == 0) {
        return;
    }
    auto* opaque = static_cast<JsTaskOpaque*>(JS_GetOpaque(val, classId));
    delete opaque;
}

JSValue jsTaskTryNext(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }

    stdiolink::Message msg;
    if (!opaque->task.tryNext(msg)) {
        return JS_NULL;
    }
    return taskMessageToJs(ctx, msg);
}

JSValue jsTaskWaitNext(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }

    int timeoutMs = -1;
    if (argc >= 1) {
        JS_ToInt32(ctx, &timeoutMs, argv[0]);
    }

    stdiolink::Message msg;
    if (!opaque->task.waitNext(msg, timeoutMs)) {
        return JS_NULL;
    }
    return taskMessageToJs(ctx, msg);
}

JSValue jsTaskGetDone(JSContext* ctx, JSValueConst thisVal) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, opaque->task.isDone() ? 1 : 0);
}

JSValue jsTaskGetExitCode(JSContext* ctx, JSValueConst thisVal) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(ctx, opaque->task.exitCode());
}

JSValue jsTaskGetErrorText(JSContext* ctx, JSValueConst thisVal) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }
    return JS_NewString(ctx, opaque->task.errorText().toUtf8().constData());
}

JSValue jsTaskGetFinalPayload(JSContext* ctx, JSValueConst thisVal) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, thisVal);
    if (!opaque) {
        return JS_EXCEPTION;
    }
    return qjsonToJsValue(ctx, opaque->task.finalPayload());
}

} // namespace

void JsTaskBinding::registerClass(JSContext* ctx) {
    if (!ctx) {
        return;
    }
    ensureTaskClass(ctx);
}

JSValue JsTaskBinding::createFromTask(JSContext* ctx, const stdiolink::Task& task) {
    const JSClassID classId = ensureTaskClass(ctx);
    if (classId == 0) {
        return JS_ThrowInternalError(ctx, "failed to register Task class");
    }

    JSValue obj = JS_NewObjectClass(ctx, classId);
    if (JS_IsException(obj)) {
        return obj;
    }

    auto* opaque = new JsTaskOpaque();
    opaque->task = task;
    JS_SetOpaque(obj, opaque);
    return obj;
}

bool JsTaskBinding::toTask(JSContext* ctx, JSValueConst value, stdiolink::Task& outTask) {
    JsTaskOpaque* opaque = getTaskOpaque(ctx, value);
    if (!opaque) {
        return false;
    }
    outTask = opaque->task;
    return true;
}

void JsTaskBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) {
        return;
    }
    s_taskClassIds.remove(reinterpret_cast<quintptr>(rt));
}

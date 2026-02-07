#include "js_driver.h"

#include <QHash>
#include <memory>

#include "js_task.h"
#include "stdiolink/host/driver.h"
#include "utils/js_convert.h"

namespace {

struct JsDriverOpaque {
    std::unique_ptr<stdiolink::Driver> driver;
};

QHash<quintptr, JSClassID> s_driverClassIds;

JSClassID classIdForRuntime(JSRuntime* rt) {
    return s_driverClassIds.value(reinterpret_cast<quintptr>(rt), 0);
}

void jsDriverFinalizer(JSRuntime* rt, JSValueConst val);
JSValue jsDriverCtor(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsDriverStart(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsDriverRequest(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsDriverQueryMeta(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsDriverTerminate(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
JSValue jsDriverGetRunning(JSContext* ctx, JSValueConst thisVal);
JSValue jsDriverGetHasMeta(JSContext* ctx, JSValueConst thisVal);

const JSCFunctionListEntry kDriverProtoFuncs[] = {
    JS_CFUNC_DEF("start", 2, jsDriverStart),
    JS_CFUNC_DEF("request", 2, jsDriverRequest),
    JS_CFUNC_DEF("queryMeta", 1, jsDriverQueryMeta),
    JS_CFUNC_DEF("terminate", 0, jsDriverTerminate),
    JS_CGETSET_DEF("running", jsDriverGetRunning, nullptr),
    JS_CGETSET_DEF("hasMeta", jsDriverGetHasMeta, nullptr),
};

JSClassID ensureDriverClass(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    const JSClassID existing = classIdForRuntime(rt);
    if (existing != 0) {
        return existing;
    }

    JsTaskBinding::registerClass(ctx);

    JSClassID classId = 0;
    JS_NewClassID(rt, &classId);

    JSClassDef clsDef{};
    clsDef.class_name = "Driver";
    clsDef.finalizer = jsDriverFinalizer;
    if (JS_NewClass(rt, classId, &clsDef) < 0) {
        return 0;
    }

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, kDriverProtoFuncs,
                               static_cast<int>(sizeof(kDriverProtoFuncs) / sizeof(kDriverProtoFuncs[0])));
    JS_SetClassProto(ctx, classId, proto);

    s_driverClassIds.insert(reinterpret_cast<quintptr>(rt), classId);
    return classId;
}

JsDriverOpaque* getDriverOpaque(JSContext* ctx, JSValueConst thisVal) {
    const JSClassID classId = classIdForRuntime(JS_GetRuntime(ctx));
    if (classId == 0) {
        return nullptr;
    }
    return static_cast<JsDriverOpaque*>(JS_GetOpaque2(ctx, thisVal, classId));
}

void jsDriverFinalizer(JSRuntime* rt, JSValueConst val) {
    const JSClassID classId = classIdForRuntime(rt);
    if (classId == 0) {
        return;
    }
    auto* opaque = static_cast<JsDriverOpaque*>(JS_GetOpaque(val, classId));
    if (!opaque) {
        return;
    }
    if (opaque->driver) {
        opaque->driver->terminate();
    }
    delete opaque;
}

JSValue jsDriverCtor(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    const JSClassID classId = ensureDriverClass(ctx);
    if (classId == 0) {
        return JS_ThrowInternalError(ctx, "failed to register Driver class");
    }

    JSValue obj = JS_NewObjectClass(ctx, classId);
    if (JS_IsException(obj)) {
        return obj;
    }

    auto* opaque = new JsDriverOpaque();
    opaque->driver = std::make_unique<stdiolink::Driver>();
    JS_SetOpaque(obj, opaque);
    return obj;
}

JSValue jsDriverStart(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "start(program, args?): program must be a string");
    }

    const char* programC = JS_ToCString(ctx, argv[0]);
    if (!programC) {
        return JS_EXCEPTION;
    }
    const QString program = QString::fromUtf8(programC);
    JS_FreeCString(ctx, programC);

    QStringList args;
    if (argc >= 2 && JS_IsArray(argv[1])) {
        JSValue lenVal = JS_GetPropertyStr(ctx, argv[1], "length");
        uint32_t len = 0;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);

        for (uint32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], i);
            const char* s = JS_ToCString(ctx, item);
            if (s) {
                args.push_back(QString::fromUtf8(s));
                JS_FreeCString(ctx, s);
            }
            JS_FreeValue(ctx, item);
        }
    }

    const bool ok = opaque->driver->start(program, args);
    return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue jsDriverRequest(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "request(cmd, data?): cmd must be a string");
    }
    if (!opaque->driver->isRunning()) {
        return JS_ThrowInternalError(ctx, "driver is not running");
    }

    const char* cmdC = JS_ToCString(ctx, argv[0]);
    if (!cmdC) {
        return JS_EXCEPTION;
    }
    const QString cmd = QString::fromUtf8(cmdC);
    JS_FreeCString(ctx, cmdC);

    QJsonObject data;
    if (argc >= 2) {
        data = jsValueToQJsonObject(ctx, argv[1]);
    }

    const stdiolink::Task task = opaque->driver->request(cmd, data);
    return JsTaskBinding::createFromTask(ctx, task);
}

JSValue jsDriverQueryMeta(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }

    int timeoutMs = 5000;
    if (argc >= 1) {
        JS_ToInt32(ctx, &timeoutMs, argv[0]);
    }

    const auto* meta = opaque->driver->queryMeta(timeoutMs);
    if (!meta) {
        return JS_NULL;
    }
    return qjsonObjectToJsValue(ctx, meta->toJson());
}

JSValue jsDriverTerminate(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }
    opaque->driver->terminate();
    return JS_UNDEFINED;
}

JSValue jsDriverGetRunning(JSContext* ctx, JSValueConst thisVal) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, opaque->driver->isRunning() ? 1 : 0);
}

JSValue jsDriverGetHasMeta(JSContext* ctx, JSValueConst thisVal) {
    JsDriverOpaque* opaque = getDriverOpaque(ctx, thisVal);
    if (!opaque || !opaque->driver) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, opaque->driver->hasMeta() ? 1 : 0);
}

} // namespace

void JsDriverBinding::registerClass(JSContext* ctx) {
    if (!ctx) {
        return;
    }
    ensureDriverClass(ctx);
}

JSValue JsDriverBinding::getConstructor(JSContext* ctx) {
    const JSClassID classId = ensureDriverClass(ctx);
    if (classId == 0) {
        return JS_ThrowInternalError(ctx, "failed to register Driver class");
    }

    JSValue ctor = JS_NewCFunction2(ctx, jsDriverCtor, "Driver", 0, JS_CFUNC_constructor, 0);
    JSValue proto = JS_GetClassProto(ctx, classId);
    JS_SetConstructor(ctx, ctor, proto);
    JS_FreeValue(ctx, proto);
    return ctor;
}

void JsDriverBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) {
        return;
    }
    s_driverClassIds.remove(reinterpret_cast<quintptr>(rt));
}

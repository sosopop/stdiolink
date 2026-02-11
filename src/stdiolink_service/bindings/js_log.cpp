#include "js_log.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <quickjs.h>

#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

/// Logger 实例的 opaque 数据
struct LoggerData {
    QJsonObject baseFields;
};

JSClassID s_loggerClassId = 0;

void loggerFinalizer(JSRuntime*, JSValue val) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(val, s_loggerClassId));
    delete data;
}

JSClassDef s_loggerClassDef = {
    "Logger",
    loggerFinalizer,
    nullptr, nullptr, nullptr
};

/// 将 JS 值转为 QString（非字符串自动 toString）
QString valueToString(JSContext* ctx, JSValueConst val) {
    if (JS_IsString(val)) {
        const char* s = JS_ToCString(ctx, val);
        QString result = QString::fromUtf8(s);
        JS_FreeCString(ctx, s);
        return result;
    }
    JSValue str = JS_ToString(ctx, val);
    if (JS_IsException(str)) return QStringLiteral("[object]");
    const char* s = JS_ToCString(ctx, str);
    QString result = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, str);
    return result;
}

/// 核心日志输出函数
JSValue emitLog(JSContext* ctx, const char* level,
                JSValueConst thisVal,
                int argc, JSValueConst* argv) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(thisVal, s_loggerClassId));
    if (!data) {
        return JS_ThrowTypeError(ctx,
            "log.%s: invalid logger", level);
    }

    QString msg;
    if (argc >= 1) {
        msg = valueToString(ctx, argv[0]);
    }

    QJsonObject mergedFields = data->baseFields;
    if (argc >= 2 && JS_IsObject(argv[1])
        && !JS_IsNull(argv[1])) {
        QJsonObject callFields =
            jsValueToQJsonObject(ctx, argv[1]);
        for (auto it = callFields.begin();
             it != callFields.end(); ++it) {
            mergedFields[it.key()] = it.value();
        }
    } else if (argc >= 2 && !JS_IsUndefined(argv[1])
               && !JS_IsNull(argv[1])) {
        return JS_ThrowTypeError(ctx,
            "log.%s: fields must be an object", level);
    }

    QJsonObject logObj;
    logObj["ts"] = QDateTime::currentDateTimeUtc()
                       .toString(Qt::ISODateWithMs);
    logObj["level"] = QString::fromUtf8(level);
    logObj["msg"] = msg;
    if (!mergedFields.isEmpty()) {
        logObj["fields"] = mergedFields;
    }

    QByteArray line = QJsonDocument(logObj)
                          .toJson(QJsonDocument::Compact);

    if (qstrcmp(level, "debug") == 0) {
        qDebug().noquote() << line;
    } else if (qstrcmp(level, "info") == 0) {
        qInfo().noquote() << line;
    } else if (qstrcmp(level, "warn") == 0) {
        qWarning().noquote() << line;
    } else if (qstrcmp(level, "error") == 0) {
        qCritical().noquote() << line;
    }
    return JS_UNDEFINED;
}

JSValue jsLogDebug(JSContext* ctx, JSValueConst thisVal,
                   int argc, JSValueConst* argv) {
    return emitLog(ctx, "debug", thisVal, argc, argv);
}

JSValue jsLogInfo(JSContext* ctx, JSValueConst thisVal,
                  int argc, JSValueConst* argv) {
    return emitLog(ctx, "info", thisVal, argc, argv);
}

JSValue jsLogWarn(JSContext* ctx, JSValueConst thisVal,
                  int argc, JSValueConst* argv) {
    return emitLog(ctx, "warn", thisVal, argc, argv);
}

JSValue jsLogError(JSContext* ctx, JSValueConst thisVal,
                   int argc, JSValueConst* argv) {
    return emitLog(ctx, "error", thisVal, argc, argv);
}

JSValue jsChild(JSContext* ctx, JSValueConst thisVal,
                int argc, JSValueConst* argv);

/// 创建 Logger JS 对象（内部复用）
JSValue createLoggerObject(JSContext* ctx,
                           const QJsonObject& baseFields) {
    JSValue obj = JS_NewObjectClass(ctx, s_loggerClassId);
    if (JS_IsException(obj)) return obj;

    auto* data = new LoggerData{baseFields};
    JS_SetOpaque(obj, data);

    JS_SetPropertyStr(ctx, obj, "debug",
        JS_NewCFunction(ctx, jsLogDebug, "debug", 2));
    JS_SetPropertyStr(ctx, obj, "info",
        JS_NewCFunction(ctx, jsLogInfo, "info", 2));
    JS_SetPropertyStr(ctx, obj, "warn",
        JS_NewCFunction(ctx, jsLogWarn, "warn", 2));
    JS_SetPropertyStr(ctx, obj, "error",
        JS_NewCFunction(ctx, jsLogError, "error", 2));
    JS_SetPropertyStr(ctx, obj, "child",
        JS_NewCFunction(ctx, jsChild, "child", 1));
    return obj;
}

JSValue jsChild(JSContext* ctx, JSValueConst thisVal,
                int argc, JSValueConst* argv) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(thisVal, s_loggerClassId));
    if (!data) {
        return JS_ThrowTypeError(ctx,
            "log.child: invalid logger");
    }

    QJsonObject merged = data->baseFields;
    if (argc >= 1 && JS_IsObject(argv[0])
        && !JS_IsNull(argv[0])) {
        QJsonObject extra =
            jsValueToQJsonObject(ctx, argv[0]);
        for (auto it = extra.begin();
             it != extra.end(); ++it) {
            merged[it.key()] = it.value();
        }
    } else if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "log.child: extraFields must be an object");
    }

    return createLoggerObject(ctx, merged);
}

JSValue jsCreateLogger(JSContext* ctx, JSValueConst,
                       int argc, JSValueConst* argv) {
    QJsonObject baseFields;
    if (argc >= 1 && JS_IsObject(argv[0])
        && !JS_IsNull(argv[0])) {
        baseFields = jsValueToQJsonObject(ctx, argv[0]);
    }
    return createLoggerObject(ctx, baseFields);
}

int logModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "createLogger",
        JS_NewCFunction(ctx, jsCreateLogger,
                        "createLogger", 1));
    return 0;
}

} // namespace

void JsLogBinding::registerLoggerClass(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    if (s_loggerClassId == 0) {
        JS_NewClassID(rt, &s_loggerClassId);
    }
    // Must register with each new runtime (JS_NewClass is a no-op if already registered)
    JS_NewClass(rt, s_loggerClassId, &s_loggerClassDef);
}

JSModuleDef* JsLogBinding::initModule(JSContext* ctx,
                                       const char* name) {
    registerLoggerClass(ctx);
    JSModuleDef* module = JS_NewCModule(ctx, name,
                                         logModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "createLogger");
    return module;
}

} // namespace stdiolink_service

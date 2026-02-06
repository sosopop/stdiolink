#include "console_bridge.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QStringList>
#include "quickjs.h"

namespace {

QString jsValueToString(JSContext* ctx, JSValueConst value) {
    if (JS_IsObject(value)) {
        JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
        if (!JS_IsException(json)) {
            const char* text = JS_ToCString(ctx, json);
            if (text) {
                const QString s = QString::fromUtf8(text);
                JS_FreeCString(ctx, text);
                JS_FreeValue(ctx, json);
                return s;
            }
        }
        JS_FreeValue(ctx, json);
    }

    const char* text = JS_ToCString(ctx, value);
    if (!text) {
        return "<unprintable>";
    }

    const QString s = QString::fromUtf8(text);
    JS_FreeCString(ctx, text);
    return s;
}

QString joinArgs(JSContext* ctx, int argc, JSValueConst* argv) {
    QStringList parts;
    parts.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        parts.push_back(jsValueToString(ctx, argv[i]));
    }
    return parts.join(' ');
}

JSValue jsConsoleLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    qDebug().noquote() << joinArgs(ctx, argc, argv);
    return JS_UNDEFINED;
}

JSValue jsConsoleInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    qInfo().noquote() << joinArgs(ctx, argc, argv);
    return JS_UNDEFINED;
}

JSValue jsConsoleWarn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    qWarning().noquote() << joinArgs(ctx, argc, argv);
    return JS_UNDEFINED;
}

JSValue jsConsoleError(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    qCritical().noquote() << joinArgs(ctx, argc, argv);
    return JS_UNDEFINED;
}

} // namespace

void ConsoleBridge::install(JSContext* ctx) {
    if (!ctx) {
        return;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, jsConsoleLog, "log", 1));
    JS_SetPropertyStr(ctx, console, "info", JS_NewCFunction(ctx, jsConsoleInfo, "info", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, jsConsoleWarn, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, jsConsoleError, "error", 1));

    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}


#include "js_process.h"

#include <QProcess>
#include <QProcessEnvironment>
#include "js_config.h"

namespace {

QString toQString(JSContext* ctx, JSValueConst value) {
    const char* s = JS_ToCString(ctx, value);
    if (!s) {
        return QString();
    }
    const QString out = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    return out;
}

bool parseArgArray(JSContext* ctx, JSValueConst arrayValue, QStringList& outArgs) {
    if (JS_IsUndefined(arrayValue) || JS_IsNull(arrayValue)) {
        return true;
    }
    if (!JS_IsArray(arrayValue)) {
        JS_ThrowTypeError(ctx, "exec(program, args?, options?): args must be an array");
        return false;
    }

    JSValue lenValue = JS_GetPropertyStr(ctx, arrayValue, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenValue);
    JS_FreeValue(ctx, lenValue);

    outArgs.clear();
    outArgs.reserve(static_cast<int>(len));
    for (uint32_t i = 0; i < len; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, arrayValue, i);
        if (JS_IsException(item)) {
            return false;
        }
        outArgs.push_back(toQString(ctx, item));
        JS_FreeValue(ctx, item);
    }
    return true;
}

bool parseEnvObject(JSContext* ctx, JSValueConst envObj, QProcessEnvironment& env) {
    JSPropertyEnum* props = nullptr;
    uint32_t propCount = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &propCount, envObj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return false;
    }

    for (uint32_t i = 0; i < propCount; ++i) {
        const char* keyC = JS_AtomToCString(ctx, props[i].atom);
        if (!keyC) {
            continue;
        }
        const QString key = QString::fromUtf8(keyC);
        JS_FreeCString(ctx, keyC);

        JSValue val = JS_GetProperty(ctx, envObj, props[i].atom);
        if (JS_IsException(val)) {
            JS_FreePropertyEnum(ctx, props, propCount);
            return false;
        }
        env.insert(key, toQString(ctx, val));
        JS_FreeValue(ctx, val);
    }

    JS_FreePropertyEnum(ctx, props, propCount);
    return true;
}

bool parseOptions(JSContext* ctx,
                  JSValueConst optionsValue,
                  QString& cwd,
                  int& timeoutMs,
                  QByteArray& inputData,
                  QProcessEnvironment& env) {
    if (JS_IsUndefined(optionsValue) || JS_IsNull(optionsValue)) {
        return true;
    }
    if (!JS_IsObject(optionsValue)) {
        JS_ThrowTypeError(ctx, "exec(program, args?, options?): options must be an object");
        return false;
    }

    JSValue cwdValue = JS_GetPropertyStr(ctx, optionsValue, "cwd");
    if (JS_IsException(cwdValue)) {
        return false;
    }
    if (!JS_IsUndefined(cwdValue) && !JS_IsNull(cwdValue)) {
        cwd = toQString(ctx, cwdValue);
    }
    JS_FreeValue(ctx, cwdValue);

    JSValue timeoutValue = JS_GetPropertyStr(ctx, optionsValue, "timeout");
    if (JS_IsException(timeoutValue)) {
        return false;
    }
    if (JS_IsNumber(timeoutValue)) {
        JS_ToInt32(ctx, &timeoutMs, timeoutValue);
    }
    JS_FreeValue(ctx, timeoutValue);

    JSValue inputValue = JS_GetPropertyStr(ctx, optionsValue, "input");
    if (JS_IsException(inputValue)) {
        return false;
    }
    if (!JS_IsUndefined(inputValue) && !JS_IsNull(inputValue)) {
        inputData = toQString(ctx, inputValue).toUtf8();
    }
    JS_FreeValue(ctx, inputValue);

    JSValue envValue = JS_GetPropertyStr(ctx, optionsValue, "env");
    if (JS_IsException(envValue)) {
        return false;
    }
    if (!JS_IsUndefined(envValue) && !JS_IsNull(envValue)) {
        if (!JS_IsObject(envValue)) {
            JS_FreeValue(ctx, envValue);
            JS_ThrowTypeError(ctx, "options.env must be an object");
            return false;
        }
        if (!parseEnvObject(ctx, envValue, env)) {
            JS_FreeValue(ctx, envValue);
            return false;
        }
    }
    JS_FreeValue(ctx, envValue);

    return true;
}

JSValue jsExec(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (stdiolink_service::JsConfigBinding::isDumpSchemaMode(ctx)) {
        stdiolink_service::JsConfigBinding::markBlockedSideEffect(ctx);
        return JS_ThrowInternalError(ctx, "exec() is blocked in --dump-config-schema mode");
    }
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "exec(program, args?, options?): program must be a string");
    }

    const QString program = toQString(ctx, argv[0]);
    QStringList args;
    QString cwd;
    int timeoutMs = 30000;
    QByteArray inputData;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    const JSValueConst argsValue = (argc >= 2) ? argv[1] : JS_UNDEFINED;
    if (!parseArgArray(ctx, argsValue, args)) {
        return JS_EXCEPTION;
    }

    const JSValueConst optionsValue = (argc >= 3) ? argv[2] : JS_UNDEFINED;
    if (!parseOptions(ctx, optionsValue, cwd, timeoutMs, inputData, env)) {
        return JS_EXCEPTION;
    }

    QProcess proc;
    if (!cwd.isEmpty()) {
        proc.setWorkingDirectory(cwd);
    }
    proc.setProcessEnvironment(env);
    proc.start(program, args, QIODevice::ReadWrite);

    if (!proc.waitForStarted(5000)) {
        return JS_ThrowInternalError(ctx,
                                     "exec: failed to start process: %s",
                                     program.toUtf8().constData());
    }

    if (!inputData.isEmpty()) {
        proc.write(inputData);
    }
    proc.closeWriteChannel();

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        return JS_ThrowInternalError(ctx, "exec: process timed out");
    }

    const QByteArray stdoutData = proc.readAllStandardOutput();
    const QByteArray stderrData = proc.readAllStandardError();

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "exitCode", JS_NewInt32(ctx, proc.exitCode()));
    JS_SetPropertyStr(ctx, result, "stdout",
                      JS_NewStringLen(ctx, stdoutData.constData(), static_cast<size_t>(stdoutData.size())));
    JS_SetPropertyStr(ctx, result, "stderr",
                      JS_NewStringLen(ctx, stderrData.constData(), static_cast<size_t>(stderrData.size())));
    return result;
}

} // namespace

JSValue JsProcessBinding::getExecFunction(JSContext* ctx) {
    return JS_NewCFunction(ctx, jsExec, "exec", 3);
}


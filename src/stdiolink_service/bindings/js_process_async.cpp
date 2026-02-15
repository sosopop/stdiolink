#include "js_process_async.h"

#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTimer>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

struct ProcessHandleData {
    JSContext* ctx = nullptr;
    QProcess* proc = nullptr;
    bool running = false;
    bool exitNotified = false;
    bool ownedByJsObject = false; // true for spawn handles (GC finalizer deletes)

    // Cached exit info for post-exit onExit registration
    int cachedExitCode = -1;
    QString cachedExitStatus; // "normal" or "crash"

    // JS callbacks (spawn)
    QList<JSValue> stdoutCallbacks;
    QList<JSValue> stderrCallbacks;
    QList<JSValue> exitCallbacks;

    // execAsync only
    JSValue resolve = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    QTimer* timeoutTimer = nullptr;
    QByteArray capturedStdout;
    QByteArray capturedStderr;
};

struct ProcessState {
    JSContext* ctx = nullptr;
    QSet<ProcessHandleData*> handles;
};

QHash<quintptr, ProcessState> s_states;
JSClassID s_handleClassId = 0;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ProcessState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

void freeCallbacks(JSContext* ctx, QList<JSValue>& cbs) {
    for (JSValue cb : cbs) {
        JS_FreeValue(ctx, cb);
    }
    cbs.clear();
}

void destroyHandle(ProcessHandleData* h) {
    if (!h) return;
    JSContext* ctx = h->ctx;

    if (h->timeoutTimer) {
        h->timeoutTimer->stop();
        h->timeoutTimer->deleteLater();
        h->timeoutTimer = nullptr;
    }

    if (h->proc) {
        h->proc->disconnect();
        if (h->proc->state() != QProcess::NotRunning) {
            h->proc->kill();
            h->proc->waitForFinished(200);
        }
        h->proc->deleteLater();
        h->proc = nullptr;
    }

    if (ctx) {
        freeCallbacks(ctx, h->stdoutCallbacks);
        freeCallbacks(ctx, h->stderrCallbacks);
        freeCallbacks(ctx, h->exitCallbacks);
        if (!JS_IsUndefined(h->resolve)) JS_FreeValue(ctx, h->resolve);
        if (!JS_IsUndefined(h->reject)) JS_FreeValue(ctx, h->reject);
        h->resolve = JS_UNDEFINED;
        h->reject = JS_UNDEFINED;
    }
}

// Remove a completed execAsync handle from tracking and schedule cleanup.
// Safe to call from inside QProcess signal handlers (uses deleteLater).
void cleanupExecAsyncHandle(ProcessHandleData* h) {
    if (!h) return;
    auto& state = stateFor(h->ctx);
    state.handles.remove(h);
    if (h->proc) {
        h->proc->disconnect();
        if (h->proc->state() != QProcess::NotRunning) {
            h->proc->waitForFinished(200);
        }
        h->proc->deleteLater();
        h->proc = nullptr;
    }
    if (h->timeoutTimer) {
        h->timeoutTimer->stop();
        h->timeoutTimer->deleteLater();
        h->timeoutTimer = nullptr;
    }
    // resolve/reject already freed by caller
    delete h;
}

void handleFinalizer(JSRuntime*, JSValue val) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(val, s_handleClassId));
    if (!h) return;
    // Remove from state tracking
    if (h->ctx) {
        auto key = reinterpret_cast<quintptr>(JS_GetRuntime(h->ctx));
        if (s_states.contains(key)) {
            s_states[key].handles.remove(h);
        }
    }
    destroyHandle(h);
    delete h;
}

JSClassDef s_handleClassDef = {
    "ProcessHandle",
    handleFinalizer,
    nullptr, nullptr, nullptr
};

QString toQStr(JSContext* ctx, JSValueConst val) {
    const char* s = JS_ToCString(ctx, val);
    if (!s) return {};
    QString r = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    return r;
}

bool parseArgs(JSContext* ctx, JSValueConst val, QStringList& out) {
    if (JS_IsUndefined(val) || JS_IsNull(val)) return true;
    if (!JS_IsArray(val)) {
        JS_ThrowTypeError(ctx, "args must be an array of strings");
        return false;
    }
    JSValue lenV = JS_GetPropertyStr(ctx, val, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenV);
    JS_FreeValue(ctx, lenV);
    for (uint32_t i = 0; i < len; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, val, i);
        if (!JS_IsString(item)) {
            JS_FreeValue(ctx, item);
            JS_ThrowTypeError(ctx, "args[%u] must be a string", i);
            return false;
        }
        out.append(toQStr(ctx, item));
        JS_FreeValue(ctx, item);
    }
    return true;
}

bool parseEnv(JSContext* ctx, JSValueConst obj, QProcessEnvironment& env) {
    JSPropertyEnum* props = nullptr;
    uint32_t cnt = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &cnt, obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return false;
    for (uint32_t i = 0; i < cnt; ++i) {
        const char* k = JS_AtomToCString(ctx, props[i].atom);
        if (!k) continue;
        JSValue v = JS_GetProperty(ctx, obj, props[i].atom);
        env.insert(QString::fromUtf8(k), toQStr(ctx, v));
        JS_FreeCString(ctx, k);
        JS_FreeValue(ctx, v);
    }
    JS_FreePropertyEnum(ctx, props, cnt);
    return true;
}

// ── ProcessHandle methods ──

JSValue jsHandleOnStdout(JSContext* ctx, JSValueConst thisVal,
                         int argc, JSValueConst* argv) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "onStdout: callback required");
    h->stdoutCallbacks.append(JS_DupValue(ctx, argv[0]));
    return JS_DupValue(ctx, thisVal);
}

JSValue jsHandleOnStderr(JSContext* ctx, JSValueConst thisVal,
                         int argc, JSValueConst* argv) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "onStderr: callback required");
    h->stderrCallbacks.append(JS_DupValue(ctx, argv[0]));
    return JS_DupValue(ctx, thisVal);
}

JSValue jsHandleOnExit(JSContext* ctx, JSValueConst thisVal,
                       int argc, JSValueConst* argv) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "onExit: callback required");
    h->exitCallbacks.append(JS_DupValue(ctx, argv[0]));
    // If process already exited, immediately fire the callback
    if (h->exitNotified && !h->cachedExitStatus.isEmpty()) {
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "exitCode",
            JS_NewInt32(ctx, h->cachedExitCode));
        JS_SetPropertyStr(ctx, result, "exitStatus",
            JS_NewString(ctx, h->cachedExitStatus.toUtf8().constData()));
        JSValue cb = h->exitCallbacks.last();
        JSValue args[1] = {result};
        JSValue ret = JS_Call(ctx, cb, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, result);
    }
    return JS_DupValue(ctx, thisVal);
}

JSValue jsHandleWrite(JSContext* ctx, JSValueConst thisVal,
                      int argc, JSValueConst* argv) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (!h->running || !h->proc)
        return JS_FALSE;
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "write: data must be a string");
    const char* s = JS_ToCString(ctx, argv[0]);
    h->proc->write(QByteArray(s));
    JS_FreeCString(ctx, s);
    return JS_TRUE;
}

JSValue jsHandleCloseStdin(JSContext* ctx, JSValueConst thisVal,
                           int, JSValueConst*) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (h->proc) h->proc->closeWriteChannel();
    return JS_UNDEFINED;
}

JSValue jsHandleKill(JSContext* ctx, JSValueConst thisVal,
                     int argc, JSValueConst* argv) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_ThrowTypeError(ctx, "invalid ProcessHandle");
    if (!h->proc || h->proc->state() == QProcess::NotRunning)
        return JS_UNDEFINED;

    // Cancel timeout timer on manual kill
    if (h->timeoutTimer) {
        h->timeoutTimer->stop();
        h->timeoutTimer->deleteLater();
        h->timeoutTimer = nullptr;
    }

    QString sig = "SIGTERM";
    if (argc >= 1 && JS_IsString(argv[0])) {
        sig = toQStr(ctx, argv[0]);
    }
    if (sig == "SIGKILL") {
        h->proc->kill();
    } else {
        h->proc->terminate();
    }
    return JS_UNDEFINED;
}

JSValue jsHandleGetPid(JSContext* ctx, JSValueConst thisVal) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h || !h->proc) return JS_NewInt32(ctx, -1);
    return JS_NewInt64(ctx, h->proc->processId());
}

JSValue jsHandleGetRunning(JSContext* ctx, JSValueConst thisVal) {
    auto* h = static_cast<ProcessHandleData*>(
        JS_GetOpaque(thisVal, s_handleClassId));
    if (!h) return JS_FALSE;
    return JS_NewBool(ctx, h->running);
}

// ── Handle object creation ──

JSValue createHandleObject(JSContext* ctx, ProcessHandleData* h) {
    JSValue obj = JS_NewObjectClass(ctx, s_handleClassId);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, h);

    JS_SetPropertyStr(ctx, obj, "onStdout",
        JS_NewCFunction(ctx, jsHandleOnStdout, "onStdout", 1));
    JS_SetPropertyStr(ctx, obj, "onStderr",
        JS_NewCFunction(ctx, jsHandleOnStderr, "onStderr", 1));
    JS_SetPropertyStr(ctx, obj, "onExit",
        JS_NewCFunction(ctx, jsHandleOnExit, "onExit", 1));
    JS_SetPropertyStr(ctx, obj, "write",
        JS_NewCFunction(ctx, jsHandleWrite, "write", 1));
    JS_SetPropertyStr(ctx, obj, "closeStdin",
        JS_NewCFunction(ctx, jsHandleCloseStdin, "closeStdin", 0));
    JS_SetPropertyStr(ctx, obj, "kill",
        JS_NewCFunction(ctx, jsHandleKill, "kill", 1));

    // pid and running as getters
    JSAtom pidAtom = JS_NewAtom(ctx, "pid");
    JS_DefinePropertyGetSet(ctx, obj, pidAtom,
        JS_NewCFunction2(ctx, (JSCFunction*)jsHandleGetPid, "pid", 0,
                         JS_CFUNC_getter, 0),
        JS_UNDEFINED, 0);
    JS_FreeAtom(ctx, pidAtom);

    JSAtom runAtom = JS_NewAtom(ctx, "running");
    JS_DefinePropertyGetSet(ctx, obj, runAtom,
        JS_NewCFunction2(ctx, (JSCFunction*)jsHandleGetRunning, "running", 0,
                         JS_CFUNC_getter, 0),
        JS_UNDEFINED, 0);
    JS_FreeAtom(ctx, runAtom);

    return obj;
}

// ── Wire QProcess signals for spawn mode ──

void wireSpawnSignals(ProcessHandleData* h) {
    JSContext* ctx = h->ctx;
    QProcess* proc = h->proc;

    QObject::connect(proc, &QProcess::readyReadStandardOutput, [h]() {
        if (!h->ctx || h->stdoutCallbacks.isEmpty()) return;
        QByteArray data = h->proc->readAllStandardOutput();
        JSValue str = JS_NewStringLen(h->ctx, data.constData(),
                                      static_cast<size_t>(data.size()));
        for (const JSValue& cb : h->stdoutCallbacks) {
            JSValue args[1] = {str};
            JSValue ret = JS_Call(h->ctx, cb, JS_UNDEFINED, 1, args);
            JS_FreeValue(h->ctx, ret);
        }
        JS_FreeValue(h->ctx, str);
    });

    QObject::connect(proc, &QProcess::readyReadStandardError, [h]() {
        if (!h->ctx || h->stderrCallbacks.isEmpty()) return;
        QByteArray data = h->proc->readAllStandardError();
        JSValue str = JS_NewStringLen(h->ctx, data.constData(),
                                      static_cast<size_t>(data.size()));
        for (const JSValue& cb : h->stderrCallbacks) {
            JSValue args[1] = {str};
            JSValue ret = JS_Call(h->ctx, cb, JS_UNDEFINED, 1, args);
            JS_FreeValue(h->ctx, ret);
        }
        JS_FreeValue(h->ctx, str);
    });

    QObject::connect(proc, &QProcess::finished, [h, ctx](int code, QProcess::ExitStatus status) {
        if (h->exitNotified) return;
        h->running = false;
        h->exitNotified = true;
        h->cachedExitCode = code;
        h->cachedExitStatus = (status == QProcess::CrashExit) ? "crash" : "normal";

        if (h->timeoutTimer) {
            h->timeoutTimer->stop();
            h->timeoutTimer->deleteLater();
            h->timeoutTimer = nullptr;
        }

        if (h->exitCallbacks.isEmpty()) return;
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "exitCode", JS_NewInt32(ctx, code));
        JS_SetPropertyStr(ctx, result, "exitStatus",
            JS_NewString(ctx, h->cachedExitStatus.toUtf8().constData()));
        for (const JSValue& cb : h->exitCallbacks) {
            JSValue args[1] = {result};
            JSValue ret = JS_Call(ctx, cb, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, result);
    });

    QObject::connect(proc, &QProcess::errorOccurred, [h, ctx](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart && !h->exitNotified) {
            h->running = false;
            h->exitNotified = true;
            h->cachedExitCode = -1;
            h->cachedExitStatus = "crash";
            JSValue result = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, result, "exitCode", JS_NewInt32(ctx, -1));
            JS_SetPropertyStr(ctx, result, "exitStatus", JS_NewString(ctx, "crash"));
            for (const JSValue& cb : h->exitCallbacks) {
                JSValue args[1] = {result};
                JSValue ret = JS_Call(ctx, cb, JS_UNDEFINED, 1, args);
                JS_FreeValue(ctx, ret);
            }
            JS_FreeValue(ctx, result);
        }
    });
}

// ── execAsync ──

JSValue jsExecAsync(JSContext* ctx, JSValueConst,
                    int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "execAsync: program must be a string");

    QString program = toQStr(ctx, argv[0]);
    if (program.isEmpty())
        return JS_ThrowTypeError(ctx, "execAsync: program must be a non-empty string");

    QStringList args;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (!parseArgs(ctx, argv[1], args)) return JS_EXCEPTION;
    }

    QString cwd;
    int timeoutMs = 30000;
    QByteArray inputData;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    if (argc >= 3 && JS_IsObject(argv[2]) && !JS_IsNull(argv[2])) {
        JSValueConst opts = argv[2];

        // Validate unknown keys
        static const QSet<QString> allowedKeys = {"cwd", "env", "input", "timeoutMs"};
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, opts,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                const char* k = JS_AtomToCString(ctx, props[i].atom);
                if (k && !allowedKeys.contains(QString::fromUtf8(k))) {
                    QString msg = QString("execAsync: unknown option: %1").arg(k);
                    JS_FreeCString(ctx, k);
                    JS_FreePropertyEnum(ctx, props, propCount);
                    return JS_ThrowTypeError(ctx, "%s", msg.toUtf8().constData());
                }
                if (k) JS_FreeCString(ctx, k);
            }
            JS_FreePropertyEnum(ctx, props, propCount);
        }

        JSValue cwdV = JS_GetPropertyStr(ctx, opts, "cwd");
        if (JS_IsString(cwdV)) cwd = toQStr(ctx, cwdV);
        JS_FreeValue(ctx, cwdV);

        JSValue tmV = JS_GetPropertyStr(ctx, opts, "timeoutMs");
        if (JS_IsNumber(tmV)) {
            int32_t t = 0;
            JS_ToInt32(ctx, &t, tmV);
            if (t > 0) timeoutMs = t;
        }
        JS_FreeValue(ctx, tmV);

        JSValue inV = JS_GetPropertyStr(ctx, opts, "input");
        if (JS_IsString(inV)) {
            const char* s = JS_ToCString(ctx, inV);
            inputData = QByteArray(s);
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, inV);

        JSValue envV = JS_GetPropertyStr(ctx, opts, "env");
        if (JS_IsObject(envV) && !JS_IsNull(envV)) {
            if (!parseEnv(ctx, envV, env)) {
                JS_FreeValue(ctx, envV);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, envV);
    }

    // Create Promise
    JSValue funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) return promise;

    auto& state = stateFor(ctx);
    auto* h = new ProcessHandleData();
    h->ctx = ctx;
    h->resolve = funcs[0];
    h->reject = funcs[1];
    h->running = true;
    state.handles.insert(h);

    auto* proc = new QProcess();
    h->proc = proc;
    if (!cwd.isEmpty()) proc->setWorkingDirectory(cwd);
    proc->setProcessEnvironment(env);

    // Accumulate stdout/stderr for execAsync
    QObject::connect(proc, &QProcess::readyReadStandardOutput, [h]() {
        h->capturedStdout.append(h->proc->readAllStandardOutput());
    });
    QObject::connect(proc, &QProcess::readyReadStandardError, [h]() {
        h->capturedStderr.append(h->proc->readAllStandardError());
    });

    // On finished → resolve
    QObject::connect(proc, &QProcess::finished, [h, ctx](int code, QProcess::ExitStatus) {
        if (JS_IsUndefined(h->resolve)) return;
        h->running = false;
        h->exitNotified = true;

        if (h->timeoutTimer) {
            h->timeoutTimer->stop();
            h->timeoutTimer->deleteLater();
            h->timeoutTimer = nullptr;
        }

        // Drain remaining output
        h->capturedStdout.append(h->proc->readAllStandardOutput());
        h->capturedStderr.append(h->proc->readAllStandardError());

        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "exitCode", JS_NewInt32(ctx, code));
        JS_SetPropertyStr(ctx, result, "stdout",
            JS_NewStringLen(ctx, h->capturedStdout.constData(),
                            static_cast<size_t>(h->capturedStdout.size())));
        JS_SetPropertyStr(ctx, result, "stderr",
            JS_NewStringLen(ctx, h->capturedStderr.constData(),
                            static_cast<size_t>(h->capturedStderr.size())));

        JSValue args[1] = {result};
        JSValue ret = JS_Call(ctx, h->resolve, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, result);

        JS_FreeValue(ctx, h->resolve);
        JS_FreeValue(ctx, h->reject);
        h->resolve = JS_UNDEFINED;
        h->reject = JS_UNDEFINED;
        cleanupExecAsyncHandle(h);
    });

    // FailedToStart → reject
    QObject::connect(proc, &QProcess::errorOccurred, [h, ctx](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        if (JS_IsUndefined(h->reject)) return;
        h->running = false;
        h->exitNotified = true;

        if (h->timeoutTimer) {
            h->timeoutTimer->stop();
            h->timeoutTimer->deleteLater();
            h->timeoutTimer = nullptr;
        }

        JSValue rej = h->reject;
        JS_FreeValue(ctx, h->resolve);
        h->resolve = JS_UNDEFINED;
        h->reject = JS_UNDEFINED;

        JSValue errStr = JS_NewString(ctx, "execAsync: failed to start process");
        JSValue args[1] = {errStr};
        JSValue ret = JS_Call(ctx, rej, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, errStr);
        JS_FreeValue(ctx, rej);
        cleanupExecAsyncHandle(h);
    });

    // Timeout timer
    if (timeoutMs > 0) {
        auto* timer = new QTimer();
        timer->setSingleShot(true);
        h->timeoutTimer = timer;
        QObject::connect(timer, &QTimer::timeout, [h, ctx, proc]() {
            if (JS_IsUndefined(h->reject)) return;

            // Consume resolve/reject BEFORE kill — waitForFinished inside
            // kill path could trigger the finished signal synchronously.
            JSValue rej = h->reject;
            JS_FreeValue(ctx, h->resolve);
            h->resolve = JS_UNDEFINED;
            h->reject = JS_UNDEFINED;
            h->running = false;
            h->exitNotified = true;

            proc->kill();

            JSValue errStr = JS_NewString(ctx, "execAsync: process timed out");
            JSValue args[1] = {errStr};
            JSValue ret = JS_Call(ctx, rej, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, errStr);
            JS_FreeValue(ctx, rej);
            cleanupExecAsyncHandle(h);
        });
        timer->start(timeoutMs);
    }

    // Start process
    proc->start(program, args, QIODevice::ReadWrite);
    if (!inputData.isEmpty()) {
        proc->write(inputData);
    }
    proc->closeWriteChannel();

    return promise;
}

// ── spawn ──

JSValue jsSpawn(JSContext* ctx, JSValueConst,
                int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "spawn: program must be a string");

    QString program = toQStr(ctx, argv[0]);
    if (program.isEmpty())
        return JS_ThrowTypeError(ctx, "spawn: program must be a non-empty string");

    QStringList args;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (!parseArgs(ctx, argv[1], args)) return JS_EXCEPTION;
    }

    QString cwd;
    int timeoutMs = 0;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    if (argc >= 3 && JS_IsObject(argv[2]) && !JS_IsNull(argv[2])) {
        JSValueConst opts = argv[2];

        // Validate unknown keys
        static const QSet<QString> allowedKeys = {"cwd", "env", "timeoutMs"};
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, opts,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                const char* k = JS_AtomToCString(ctx, props[i].atom);
                if (k && !allowedKeys.contains(QString::fromUtf8(k))) {
                    QString msg = QString("spawn: unknown option: %1").arg(k);
                    JS_FreeCString(ctx, k);
                    JS_FreePropertyEnum(ctx, props, propCount);
                    return JS_ThrowTypeError(ctx, "%s", msg.toUtf8().constData());
                }
                if (k) JS_FreeCString(ctx, k);
            }
            JS_FreePropertyEnum(ctx, props, propCount);
        }

        JSValue cwdV = JS_GetPropertyStr(ctx, opts, "cwd");
        if (JS_IsString(cwdV)) cwd = toQStr(ctx, cwdV);
        JS_FreeValue(ctx, cwdV);

        JSValue tmV = JS_GetPropertyStr(ctx, opts, "timeoutMs");
        if (JS_IsNumber(tmV)) {
            int32_t t = 0;
            JS_ToInt32(ctx, &t, tmV);
            if (t > 0) timeoutMs = t;
        }
        JS_FreeValue(ctx, tmV);

        JSValue envV = JS_GetPropertyStr(ctx, opts, "env");
        if (JS_IsObject(envV) && !JS_IsNull(envV)) {
            if (!parseEnv(ctx, envV, env)) {
                JS_FreeValue(ctx, envV);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, envV);
    }

    auto& state = stateFor(ctx);
    auto* h = new ProcessHandleData();
    h->ctx = ctx;
    h->running = true;
    h->ownedByJsObject = true;
    state.handles.insert(h);

    auto* proc = new QProcess();
    h->proc = proc;
    if (!cwd.isEmpty()) proc->setWorkingDirectory(cwd);
    proc->setProcessEnvironment(env);

    wireSpawnSignals(h);
    proc->start(program, args, QIODevice::ReadWrite);

    // Timeout timer for spawn
    if (timeoutMs > 0) {
        auto* timer = new QTimer();
        timer->setSingleShot(true);
        h->timeoutTimer = timer;
        QObject::connect(timer, &QTimer::timeout, [h, proc]() {
            if (proc->state() != QProcess::NotRunning) {
                proc->kill();
            }
        });
        timer->start(timeoutMs);
    }

    return createHandleObject(ctx, h);
}

int processAsyncModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "execAsync",
        JS_NewCFunction(ctx, jsExecAsync, "execAsync", 3));
    JS_SetModuleExport(ctx, module, "spawn",
        JS_NewCFunction(ctx, jsSpawn, "spawn", 3));
    return 0;
}

} // namespace

void JsProcessAsyncBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) {
        s_states.insert(key, ProcessState{});
    }
    if (s_handleClassId == 0) {
        JS_NewClassID(rt, &s_handleClassId);
    }
    JS_NewClass(rt, s_handleClassId, &s_handleClassDef);
}

void JsProcessAsyncBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) return;
    auto& state = s_states[key];
    for (auto* h : state.handles) {
        destroyHandle(h);
        if (!h->ownedByJsObject) delete h;
    }
    state.handles.clear();
    s_states.remove(key);
}

JSModuleDef* JsProcessAsyncBinding::initModule(JSContext* ctx,
                                                const char* name) {
    stateFor(ctx).ctx = ctx;
    JSModuleDef* module = JS_NewCModule(ctx, name,
                                         processAsyncModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "execAsync");
    JS_AddModuleExport(ctx, module, "spawn");
    return module;
}

void JsProcessAsyncBinding::reset(JSContext* ctx) {
    auto& state = stateFor(ctx);
    for (auto* h : state.handles) {
        destroyHandle(h);
        if (!h->ownedByJsObject) delete h;
    }
    state.handles.clear();
}

bool JsProcessAsyncBinding::hasPending(JSContext* ctx) {
    auto& state = stateFor(ctx);
    for (auto* h : state.handles) {
        if (h->running) return true;
        if (!JS_IsUndefined(h->resolve)) return true;
    }
    return false;
}

} // namespace stdiolink_service

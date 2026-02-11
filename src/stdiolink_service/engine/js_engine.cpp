#include "js_engine.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <quickjs.h>
#include "bindings/js_config.h"
#include "bindings/js_constants.h"
#include "bindings/js_time.h"
#include "bindings/js_http.h"
#include "bindings/js_driver.h"
#include "bindings/js_process_async.h"
#include "bindings/js_task.h"
#include "module_loader.h"

JsEngine::JsEngine() {
    m_rt = JS_NewRuntime();
    if (!m_rt) {
        qCritical() << "Failed to create QuickJS runtime";
        return;
    }

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        qCritical() << "Failed to create QuickJS context";
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
        return;
    }

    JS_SetMemoryLimit(m_rt, 256ull * 1024ull * 1024ull);
    JS_SetMaxStackSize(m_rt, 8ull * 1024ull * 1024ull);
    ModuleLoader::install(m_ctx);
}

JsEngine::~JsEngine() {
    JSRuntime* oldRt = m_rt;
    // Detach bindings before freeing context/runtime so cached JSValues can be freed
    JsDriverBinding::detachRuntime(oldRt);
    JsTaskBinding::detachRuntime(oldRt);
    stdiolink_service::JsConfigBinding::detachRuntime(oldRt);
    stdiolink_service::JsConstantsBinding::detachRuntime(oldRt);
    stdiolink_service::JsTimeBinding::detachRuntime(oldRt);
    stdiolink_service::JsHttpBinding::detachRuntime(oldRt);
    stdiolink_service::JsProcessAsyncBinding::detachRuntime(oldRt);
    if (m_ctx) {
        JS_FreeContext(m_ctx);
        m_ctx = nullptr;
    }
    if (m_rt) {
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
    }
}

void JsEngine::registerModule(const QString& name, JSModuleDef* (*init)(JSContext*, const char*)) {
    Q_UNUSED(m_ctx);
    ModuleLoader::addBuiltin(name, init);
}

int JsEngine::evalFile(const QString& filePath) {
    if (!m_ctx) {
        qCritical() << "QuickJS context is not initialized";
        return 1;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open file:" << filePath;
        return 2;
    }

    const QByteArray code = file.readAll();
    file.close();

    const QByteArray evalName = QFileInfo(filePath).absoluteFilePath().toUtf8();
    JSValue val = JS_Eval(m_ctx, code.constData(), static_cast<size_t>(code.size()),
                          evalName.constData(), JS_EVAL_TYPE_MODULE);
    if (JS_IsException(val)) {
        printException(m_ctx);
        JS_FreeValue(m_ctx, val);
        return 1;
    }

    // QuickJS may represent module-evaluation failures as a rejected Promise
    // instead of returning JS_EXCEPTION directly.
    if (JS_IsPromise(val) && JS_PromiseState(m_ctx, val) == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(m_ctx, val);
        const char* reasonText = JS_ToCString(m_ctx, reason);
        if (reasonText) {
            qCritical().noquote() << QString::fromUtf8(reasonText);
            JS_FreeCString(m_ctx, reasonText);
        } else {
            qCritical() << "JavaScript module evaluation failed";
        }

        JSValue stack = JS_GetPropertyStr(m_ctx, reason, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* stackText = JS_ToCString(m_ctx, stack);
            if (stackText) {
                qCritical().noquote() << QString::fromUtf8(stackText);
                JS_FreeCString(m_ctx, stackText);
            }
        }
        JS_FreeValue(m_ctx, stack);
        JS_FreeValue(m_ctx, reason);
        JS_FreeValue(m_ctx, val);
        return 1;
    }

    JS_FreeValue(m_ctx, val);
    return 0;
}

bool JsEngine::executePendingJobs() {
    if (!m_rt) {
        return false;
    }

    JSContext* pctx = nullptr;
    const int ret = JS_ExecutePendingJob(m_rt, &pctx);
    if (ret < 0) {
        m_jobError = true;
        printException(pctx ? pctx : m_ctx);
        return false;
    }
    return ret > 0;
}

bool JsEngine::hasPendingJobs() const {
    if (!m_rt) {
        return false;
    }
    return JS_IsJobPending(m_rt) > 0;
}

void JsEngine::printException(JSContext* ctx) const {
    if (!ctx) {
        qCritical() << "Unknown JavaScript exception";
        return;
    }

    JSValue exception = JS_GetException(ctx);
    const char* excText = JS_ToCString(ctx, exception);
    if (excText) {
        qCritical().noquote() << QString::fromUtf8(excText);
        JS_FreeCString(ctx, excText);
    }

    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack)) {
        const char* stackText = JS_ToCString(ctx, stack);
        if (stackText) {
            qCritical().noquote() << QString::fromUtf8(stackText);
            JS_FreeCString(ctx, stackText);
        }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
}

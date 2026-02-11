#include "js_http.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <quickjs.h>

#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

struct PendingRequest {
    JSValue resolve = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    QNetworkReply* reply = nullptr;
    QTimer* timer = nullptr;
    bool parseJson = false;
};

struct HttpState {
    QNetworkAccessManager* nam = nullptr;
    QHash<int, PendingRequest> pending;
    int nextId = 0;
    JSContext* ctx = nullptr;
};

QHash<quintptr, HttpState> s_states;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

HttpState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

JSValue buildResponse(JSContext* ctx, QNetworkReply* reply, bool parseJson) {
    int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewInt32(ctx, status));

    // Response headers (merge same-name headers with comma)
    QMap<QString, QStringList> headerMap;
    for (const auto& pair : reply->rawHeaderPairs()) {
        const QString key = QString::fromUtf8(pair.first).toLower();
        const QString val = QString::fromUtf8(pair.second);
        headerMap[key].append(val);
    }
    JSValue headers = JS_NewObject(ctx);
    for (auto it = headerMap.begin(); it != headerMap.end(); ++it) {
        JS_SetPropertyStr(ctx, headers,
            it.key().toUtf8().constData(),
            JS_NewString(ctx, it.value().join(", ").toUtf8().constData()));
    }
    JS_SetPropertyStr(ctx, obj, "headers", headers);

    JS_SetPropertyStr(ctx, obj, "bodyText",
        JS_NewString(ctx, body.constData()));

    // Auto-parse JSON
    bool shouldParse = parseJson;
    if (!shouldParse) {
        QString ct = reply->header(
            QNetworkRequest::ContentTypeHeader).toString();
        if (ct.contains("application/json")) {
            shouldParse = true;
        }
    }
    if (shouldParse) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError) {
            JS_FreeValue(ctx, obj);
            return JS_UNDEFINED; // caller will reject
        }
        JS_SetPropertyStr(ctx, obj, "bodyJson",
            qjsonToJsValue(ctx, doc.isObject()
                ? QJsonValue(doc.object())
                : QJsonValue(doc.array())));
    }
    return obj;
}

void mergeOptionsInto(JSContext* ctx, JSValue dst, JSValueConst src,
                      const QList<QByteArray>& skip) {
    JSPropertyEnum* props = nullptr;
    uint32_t propCount = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &propCount, src,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return;
    }
    for (uint32_t i = 0; i < propCount; ++i) {
        const char* keyC = JS_AtomToCString(ctx, props[i].atom);
        if (!keyC) continue;
        bool shouldSkip = skip.contains(QByteArray(keyC));
        JS_FreeCString(ctx, keyC);
        if (shouldSkip) continue;
        JSValue v = JS_GetProperty(ctx, src, props[i].atom);
        if (!JS_IsException(v)) {
            JS_SetProperty(ctx, dst, props[i].atom, v);
        }
    }
    JS_FreePropertyEnum(ctx, props, propCount);
}

JSValue jsRequest(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "http.request: options must be an object");
    }
    JSValueConst opts = argv[0];

    // url (required)
    JSValue urlVal = JS_GetPropertyStr(ctx, opts, "url");
    if (!JS_IsString(urlVal)) {
        JS_FreeValue(ctx, urlVal);
        return JS_ThrowTypeError(ctx, "http.request: options.url must be a string");
    }
    const char* urlStr = JS_ToCString(ctx, urlVal);
    QUrl url(QString::fromUtf8(urlStr));
    JS_FreeCString(ctx, urlStr);
    JS_FreeValue(ctx, urlVal);

    if (!url.isValid() || url.scheme().isEmpty()) {
        return JS_ThrowTypeError(ctx, "http.request: invalid URL");
    }

    // method (default GET)
    JSValue methodVal = JS_GetPropertyStr(ctx, opts, "method");
    QByteArray method = "GET";
    if (JS_IsString(methodVal)) {
        const char* m = JS_ToCString(ctx, methodVal);
        method = QByteArray(m).toUpper();
        JS_FreeCString(ctx, m);
    }
    JS_FreeValue(ctx, methodVal);

    // query params
    JSValue queryVal = JS_GetPropertyStr(ctx, opts, "query");
    if (JS_IsObject(queryVal) && !JS_IsNull(queryVal)) {
        QUrlQuery q;
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, queryVal,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                const char* keyC = JS_AtomToCString(ctx, props[i].atom);
                if (!keyC) continue;
                const QString key = QString::fromUtf8(keyC);
                JS_FreeCString(ctx, keyC);
                JSValue v = JS_GetProperty(ctx, queryVal, props[i].atom);
                const char* valC = JS_ToCString(ctx, v);
                if (valC) {
                    q.addQueryItem(key, QString::fromUtf8(valC));
                    JS_FreeCString(ctx, valC);
                }
                JS_FreeValue(ctx, v);
            }
            JS_FreePropertyEnum(ctx, props, propCount);
            url.setQuery(q);
        }
    }
    JS_FreeValue(ctx, queryVal);

    QNetworkRequest req(url);

    // headers
    JSValue headersVal = JS_GetPropertyStr(ctx, opts, "headers");
    if (JS_IsObject(headersVal) && !JS_IsNull(headersVal)) {
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, headersVal,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                const char* keyC = JS_AtomToCString(ctx, props[i].atom);
                if (!keyC) continue;
                const QByteArray key = QByteArray(keyC);
                JS_FreeCString(ctx, keyC);
                JSValue v = JS_GetProperty(ctx, headersVal, props[i].atom);
                const char* valC = JS_ToCString(ctx, v);
                if (valC) {
                    req.setRawHeader(key, QByteArray(valC));
                    JS_FreeCString(ctx, valC);
                }
                JS_FreeValue(ctx, v);
            }
            JS_FreePropertyEnum(ctx, props, propCount);
        }
    }
    JS_FreeValue(ctx, headersVal);

    // body
    QByteArray bodyData;
    JSValue bodyVal = JS_GetPropertyStr(ctx, opts, "body");
    if (JS_IsObject(bodyVal) && !JS_IsNull(bodyVal)) {
        QJsonObject jsonObj = jsValueToQJsonObject(ctx, bodyVal);
        bodyData = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    } else if (JS_IsString(bodyVal)) {
        const char* b = JS_ToCString(ctx, bodyVal);
        bodyData = QByteArray(b);
        JS_FreeCString(ctx, b);
    }
    JS_FreeValue(ctx, bodyVal);

    // parseJson
    JSValue pjVal = JS_GetPropertyStr(ctx, opts, "parseJson");
    bool parseJson = JS_ToBool(ctx, pjVal);
    JS_FreeValue(ctx, pjVal);

    // Create Promise
    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) return promise;

    auto& state = stateFor(ctx);
    if (!state.nam) {
        state.nam = new QNetworkAccessManager();
    }

    PendingRequest pending;
    pending.resolve = resolvingFuncs[0];
    pending.reject = resolvingFuncs[1];
    pending.parseJson = parseJson;

    QNetworkReply* reply = state.nam->sendCustomRequest(req, method, bodyData);
    pending.reply = reply;

    // Timeout via manual QTimer
    JSValue timeoutVal = JS_GetPropertyStr(ctx, opts, "timeoutMs");
    if (JS_IsNumber(timeoutVal)) {
        int32_t timeoutMs = 0;
        JS_ToInt32(ctx, &timeoutMs, timeoutVal);
        if (timeoutMs > 0) {
            QTimer* t = new QTimer();
            t->setSingleShot(true);
            QObject::connect(t, &QTimer::timeout, reply, &QNetworkReply::abort);
            pending.timer = t;
            t->start(timeoutMs);
        }
    }
    JS_FreeValue(ctx, timeoutVal);

    int reqId = state.nextId++;
    state.pending.insert(reqId, pending);

    QObject::connect(reply, &QNetworkReply::finished, [ctx, reqId]() {
        auto& st = stateFor(ctx);
        if (!st.pending.contains(reqId)) return;
        auto& p = st.pending[reqId];
        if (JS_IsUndefined(p.resolve)) return;

        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
            p.timer = nullptr;
        }

        if (p.reply->error() != QNetworkReply::NoError
            && p.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isNull()) {
            // transport error -> reject
            QString errMsg = p.reply->errorString();
            JSValue err = JS_NewString(ctx, errMsg.toUtf8().constData());
            JSValue args[1] = {err};
            JSValue ret = JS_Call(ctx, p.reject, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, err);
        } else {
            JSValue resp = buildResponse(ctx, p.reply, p.parseJson);
            if (JS_IsUndefined(resp)) {
                JSValue err = JS_NewString(ctx,
                    "http.request: response is not valid JSON");
                JSValue args[1] = {err};
                JSValue ret = JS_Call(ctx, p.reject, JS_UNDEFINED, 1, args);
                JS_FreeValue(ctx, ret);
                JS_FreeValue(ctx, err);
            } else {
                JSValue args[1] = {resp};
                JSValue ret = JS_Call(ctx, p.resolve, JS_UNDEFINED, 1, args);
                JS_FreeValue(ctx, ret);
                JS_FreeValue(ctx, resp);
            }
        }

        JS_FreeValue(ctx, p.resolve);
        JS_FreeValue(ctx, p.reject);
        p.reply->deleteLater();
        st.pending.remove(reqId);
    });

    return promise;
}

JSValue jsGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "http.get: url must be a string");
    }
    JSValue opts = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, opts, "method", JS_NewString(ctx, "GET"));
    JS_SetPropertyStr(ctx, opts, "url", JS_DupValue(ctx, argv[0]));
    if (argc >= 2 && JS_IsObject(argv[1])) {
        mergeOptionsInto(ctx, opts, argv[1], {"method", "url"});
    }
    JSValue callArgs[1] = {opts};
    JSValue ret = jsRequest(ctx, JS_UNDEFINED, 1, callArgs);
    JS_FreeValue(ctx, opts);
    return ret;
}

JSValue jsPost(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "http.post: url must be a string");
    }
    JSValue opts = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, opts, "method", JS_NewString(ctx, "POST"));
    JS_SetPropertyStr(ctx, opts, "url", JS_DupValue(ctx, argv[0]));
    if (argc >= 2) {
        JS_SetPropertyStr(ctx, opts, "body", JS_DupValue(ctx, argv[1]));
    }
    if (argc >= 3 && JS_IsObject(argv[2])) {
        mergeOptionsInto(ctx, opts, argv[2], {"method", "url", "body"});
    }
    JSValue callArgs[1] = {opts};
    JSValue ret = jsRequest(ctx, JS_UNDEFINED, 1, callArgs);
    JS_FreeValue(ctx, opts);
    return ret;
}

int httpModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "request",
        JS_NewCFunction(ctx, jsRequest, "request", 1));
    JS_SetModuleExport(ctx, module, "get",
        JS_NewCFunction(ctx, jsGet, "get", 2));
    JS_SetModuleExport(ctx, module, "post",
        JS_NewCFunction(ctx, jsPost, "post", 3));
    return 0;
}

} // namespace

void JsHttpBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) {
        s_states.insert(key, HttpState{});
    }
}

void JsHttpBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) return;
    auto& state = s_states[key];
    for (auto& p : state.pending) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
            p.timer = nullptr;
        }
        if (p.reply) {
            p.reply->disconnect();
            p.reply->abort();
            p.reply->deleteLater();
            p.reply = nullptr;
        }
        if (!JS_IsUndefined(p.resolve) && state.ctx) {
            JS_FreeValue(state.ctx, p.resolve);
            JS_FreeValue(state.ctx, p.reject);
        }
        p.resolve = JS_UNDEFINED;
        p.reject = JS_UNDEFINED;
    }
    state.pending.clear();
    delete state.nam;
    state.nam = nullptr;
    s_states.remove(key);
}

JSModuleDef* JsHttpBinding::initModule(JSContext* ctx, const char* name) {
    stateFor(ctx).ctx = ctx;
    JSModuleDef* module = JS_NewCModule(ctx, name, httpModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "request");
    JS_AddModuleExport(ctx, module, "get");
    JS_AddModuleExport(ctx, module, "post");
    return module;
}

void JsHttpBinding::reset(JSContext* ctx) {
    auto& state = stateFor(ctx);
    for (auto& p : state.pending) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
        }
        if (p.reply) {
            p.reply->disconnect();
            p.reply->abort();
            p.reply->deleteLater();
        }
        if (!JS_IsUndefined(p.resolve) && state.ctx) {
            JS_FreeValue(state.ctx, p.resolve);
            JS_FreeValue(state.ctx, p.reject);
        }
    }
    state.pending.clear();
}

bool JsHttpBinding::hasPending(JSContext* ctx) {
    auto& state = stateFor(ctx);
    return !state.pending.isEmpty();
}

} // namespace stdiolink_service

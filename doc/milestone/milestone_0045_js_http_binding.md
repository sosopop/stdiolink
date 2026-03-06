# 里程碑 45：JS HTTP 基础库绑定

> **前置条件**: 里程碑 44 已完成
> **目标**: 实现 `stdiolink/http` 异步 HTTP 客户端能力，支撑服务脚本与外部系统集成

---

## 1. 目标

- 新增 `stdiolink/http` 模块
- 提供 API：`request/get/post`
- 支持超时、请求头、query 参数、JSON 自动解析
- 明确 transport error 与 HTTP status 的语义边界

---

## 2. 设计原则（强约束）

- **简约**: 先支持最小 REST 场景，不实现流式下载、重试策略、连接池调优接口
- **可靠**: 超时和网络错误可观测、可捕获
- **稳定**: 返回结构固定，避免版本抖动
- **避免过度设计**: 不引入第三方 HTTP 库，使用 Qt Network 原生能力

---

## 3. 范围与非目标

### 3.1 范围（M45 内）

- `stdiolink/http` 模块
- `request/get/post` Promise API
- 响应对象标准化
- 全面单元测试

### 3.2 非目标（M45 外）

- 不实现 multipart 上传
- 不实现自动重试/熔断
- 不实现 cookie jar 策略配置

---

## 4. 技术方案

### 4.1 模块接口

```js
import { request, get, post } from "stdiolink/http";
```

`request(options)`：

- `method`、`url`、`headers`、`query`、`timeoutMs`、`body`、`parseJson`

返回：

```js
{
  status: number,
  headers: Record<string, string>,
  bodyText: string,
  bodyJson?: any
}
```

### 4.2 语义约束

- 网络错误/超时：Promise reject
- HTTP 4xx/5xx：Promise resolve（由 `status` 表达）
- `parseJson=true` 且 JSON 非法：reject
- `body` 为 object 时自动序列化为 JSON 并设置 `Content-Type: application/json`
- `body` 为 string 时原样发送，不自动设置 Content-Type（由调用方通过 `headers` 指定）
- 响应 `headers` 中同名头合并为逗号分隔字符串（遵循 HTTP/1.1 规范）
- `parseJson` 默认行为：当响应 `Content-Type` 包含 `application/json` 时自动解析

### 4.3 创建 bindings/js_http.h

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/http 内置模块绑定
///
/// 提供异步 HTTP 客户端 API（request/get/post）。
/// 底层使用 QNetworkAccessManager，Promise 通过 QNetworkReply::finished
/// 信号桥接。绑定状态按 JSRuntime 维度隔离。
class JsHttpBinding {
public:
    /// 绑定到指定 runtime
    static void attachRuntime(JSRuntime* rt);

    /// 解绑并清理所有 pending 请求
    static void detachRuntime(JSRuntime* rt);

    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.4 创建 bindings/js_http.cpp（关键实现）

```cpp
#include "js_http.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
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
    QList<PendingRequest> pending;
    JSContext* ctx = nullptr;
};

QHash<quintptr, HttpState> s_states;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

HttpState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

/// 构建响应对象 { status, headers, bodyText, bodyJson? }
JSValue buildResponse(JSContext* ctx, QNetworkReply* reply,
                      bool parseJson) {
    int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status",
                      JS_NewInt32(ctx, status));

    // 响应头（同名头合并为逗号分隔）
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

    // JSON 自动解析
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
            return JS_UNDEFINED; // 调用方需 reject
        }
        JS_SetPropertyStr(ctx, obj, "bodyJson",
            qjsonToJsValue(ctx, doc.isObject()
                ? QJsonValue(doc.object())
                : QJsonValue(doc.array())));
    }
    return obj;
}

JSValue jsRequest(JSContext* ctx, JSValueConst,
                  int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "http.request: options must be an object");
    }
    JSValueConst opts = argv[0];

    // 提取 url（必填）
    JSValue urlVal = JS_GetPropertyStr(ctx, opts, "url");
    if (!JS_IsString(urlVal)) {
        JS_FreeValue(ctx, urlVal);
        return JS_ThrowTypeError(ctx,
            "http.request: options.url must be a string");
    }
    const char* urlStr = JS_ToCString(ctx, urlVal);
    QUrl url(QString::fromUtf8(urlStr));
    JS_FreeCString(ctx, urlStr);
    JS_FreeValue(ctx, urlVal);

    if (!url.isValid()) {
        return JS_ThrowTypeError(ctx,
            "http.request: invalid URL");
    }

    // 提取 method（默认 GET）
    JSValue methodVal = JS_GetPropertyStr(ctx, opts, "method");
    QByteArray method = "GET";
    if (JS_IsString(methodVal)) {
        const char* m = JS_ToCString(ctx, methodVal);
        method = QByteArray(m).toUpper();
        JS_FreeCString(ctx, m);
    }
    JS_FreeValue(ctx, methodVal);

    // 提取 query 参数
    JSValue queryVal = JS_GetPropertyStr(ctx, opts, "query");
    if (JS_IsObject(queryVal)) {
        QUrlQuery q;
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, queryVal,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JS_FreeValue(ctx, queryVal);
            return JS_EXCEPTION;
        }
        for (uint32_t i = 0; i < propCount; ++i) {
            const char* keyC = JS_AtomToCString(ctx, props[i].atom);
            if (!keyC) continue;
            const QString key = QString::fromUtf8(keyC);
            JS_FreeCString(ctx, keyC);

            JSValue v = JS_GetProperty(ctx, queryVal, props[i].atom);
            if (JS_IsException(v)) {
                JS_FreePropertyEnum(ctx, props, propCount);
                JS_FreeValue(ctx, queryVal);
                return JS_EXCEPTION;
            }
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
    JS_FreeValue(ctx, queryVal);

    // 构建 QNetworkRequest
    QNetworkRequest req(url);

    // 提取 headers
    JSValue headersVal = JS_GetPropertyStr(ctx, opts, "headers");
    if (JS_IsObject(headersVal)) {
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, headersVal,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JS_FreeValue(ctx, headersVal);
            return JS_EXCEPTION;
        }
        for (uint32_t i = 0; i < propCount; ++i) {
            const char* keyC = JS_AtomToCString(ctx, props[i].atom);
            if (!keyC) continue;
            const QByteArray key = QByteArray(keyC);
            JS_FreeCString(ctx, keyC);

            JSValue v = JS_GetProperty(ctx, headersVal, props[i].atom);
            if (JS_IsException(v)) {
                JS_FreePropertyEnum(ctx, props, propCount);
                JS_FreeValue(ctx, headersVal);
                return JS_EXCEPTION;
            }
            const char* valC = JS_ToCString(ctx, v);
            if (valC) {
                req.setRawHeader(key, QByteArray(valC));
                JS_FreeCString(ctx, valC);
            }
            JS_FreeValue(ctx, v);
        }
        JS_FreePropertyEnum(ctx, props, propCount);
    }
    JS_FreeValue(ctx, headersVal);

    // 提取 body
    QByteArray bodyData;
    JSValue bodyVal = JS_GetPropertyStr(ctx, opts, "body");
    if (JS_IsObject(bodyVal) && !JS_IsNull(bodyVal)) {
        // object → JSON 序列化，自动设置 Content-Type
        QJsonDocument doc(jsValueToQJsonObject(ctx, bodyVal));
        bodyData = doc.toJson(QJsonDocument::Compact);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/json");
    } else if (JS_IsString(bodyVal)) {
        const char* b = JS_ToCString(ctx, bodyVal);
        bodyData = QByteArray(b);
        JS_FreeCString(ctx, b);
    }
    JS_FreeValue(ctx, bodyVal);

    // 提取 parseJson
    JSValue pjVal = JS_GetPropertyStr(ctx, opts, "parseJson");
    bool parseJson = JS_ToBool(ctx, pjVal);
    JS_FreeValue(ctx, pjVal);

    // 创建 Promise
    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) return promise;

    auto& state = stateFor(ctx);
    if (!state.nam) {
        state.nam = new QNetworkAccessManager();
    }

    // 发送请求
    QNetworkReply* reply = state.nam->sendCustomRequest(
        req, method, bodyData);

    PendingRequest pending;
    pending.resolve = resolvingFuncs[0];
    pending.reject = resolvingFuncs[1];
    pending.reply = reply;
    pending.parseJson = parseJson;

    // 超时控制
    JSValue timeoutVal = JS_GetPropertyStr(ctx, opts, "timeoutMs");
    if (JS_IsNumber(timeoutVal)) {
        int32_t timeoutMs;
        JS_ToInt32(ctx, &timeoutMs, timeoutVal);
        if (timeoutMs > 0) {
            QTimer* timer = new QTimer();
            timer->setSingleShot(true);
            QObject::connect(timer, &QTimer::timeout, [reply]() {
                if (reply->isRunning()) reply->abort();
            });
            pending.timer = timer;
            timer->start(timeoutMs);
        }
    }
    JS_FreeValue(ctx, timeoutVal);

    int idx = state.pending.size();
    state.pending.append(pending);

    // 连接 finished 信号
    QObject::connect(reply, &QNetworkReply::finished,
        [ctx, idx]() {
            auto& st = stateFor(ctx);
            if (idx >= st.pending.size()) return;
            auto& p = st.pending[idx];
            if (JS_IsUndefined(p.resolve)) return;

            if (p.timer) {
                p.timer->stop();
                p.timer->deleteLater();
                p.timer = nullptr;
            }

            if (p.reply->error() != QNetworkReply::NoError
                && p.reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute)
                    .isNull()) {
                // transport error → reject
                QString errMsg = p.reply->errorString();
                JSValue err = JS_NewString(ctx,
                    errMsg.toUtf8().constData());
                JSValue args[1] = {err};
                JSValue ret = JS_Call(ctx, p.reject,
                    JS_UNDEFINED, 1, args);
                JS_FreeValue(ctx, ret);
                JS_FreeValue(ctx, err);
            } else {
                // HTTP response → resolve
                JSValue resp = buildResponse(ctx, p.reply,
                                             p.parseJson);
                if (JS_IsUndefined(resp)) {
                    // JSON parse failed → reject
                    JSValue err = JS_NewString(ctx,
                        "http.request: response is not valid JSON");
                    JSValue args[1] = {err};
                    JSValue ret = JS_Call(ctx, p.reject,
                        JS_UNDEFINED, 1, args);
                    JS_FreeValue(ctx, ret);
                    JS_FreeValue(ctx, err);
                } else {
                    JSValue args[1] = {resp};
                    JSValue ret = JS_Call(ctx, p.resolve,
                        JS_UNDEFINED, 1, args);
                    JS_FreeValue(ctx, ret);
                    JS_FreeValue(ctx, resp);
                }
            }

            JS_FreeValue(ctx, p.resolve);
            JS_FreeValue(ctx, p.reject);
            p.resolve = JS_UNDEFINED;
            p.reject = JS_UNDEFINED;
            p.reply->deleteLater();
            p.reply = nullptr;
        });

    return promise;
}

// get/post 为 request 的快捷封装
JSValue jsGet(JSContext* ctx, JSValueConst,
              int argc, JSValueConst* argv) {
    // get(url, options?)
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "http.get: url must be a string");
    }
    JSValue opts = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, opts, "method", JS_NewString(ctx, "GET"));
    JS_SetPropertyStr(ctx, opts, "url", JS_DupValue(ctx, argv[0]));
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, argv[1],
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JS_FreeValue(ctx, opts);
            return JS_EXCEPTION;
        }
        for (uint32_t i = 0; i < propCount; ++i) {
            const char* keyC = JS_AtomToCString(ctx, props[i].atom);
            if (!keyC) continue;
            const QByteArray key = QByteArray(keyC);
            JS_FreeCString(ctx, keyC);
            if (key == "method" || key == "url") {
                continue;
            }
            JSValue v = JS_GetProperty(ctx, argv[1], props[i].atom);
            if (JS_IsException(v)) {
                JS_FreePropertyEnum(ctx, props, propCount);
                JS_FreeValue(ctx, opts);
                return JS_EXCEPTION;
            }
            JS_SetProperty(ctx, opts, props[i].atom, v);
        }
        JS_FreePropertyEnum(ctx, props, propCount);
    }
    JSValue callArgs[1] = {opts};
    JSValue ret = jsRequest(ctx, JS_UNDEFINED, 1, callArgs);
    JS_FreeValue(ctx, opts);
    return ret;
}

JSValue jsPost(JSContext* ctx, JSValueConst,
               int argc, JSValueConst* argv) {
    // post(url, body?, options?)
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
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, argv[2],
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JS_FreeValue(ctx, opts);
            return JS_EXCEPTION;
        }
        for (uint32_t i = 0; i < propCount; ++i) {
            const char* keyC = JS_AtomToCString(ctx, props[i].atom);
            if (!keyC) continue;
            const QByteArray key = QByteArray(keyC);
            JS_FreeCString(ctx, keyC);
            if (key == "method" || key == "url" || key == "body") {
                continue;
            }
            JSValue v = JS_GetProperty(ctx, argv[2], props[i].atom);
            if (JS_IsException(v)) {
                JS_FreePropertyEnum(ctx, props, propCount);
                JS_FreeValue(ctx, opts);
                return JS_EXCEPTION;
            }
            JS_SetProperty(ctx, opts, props[i].atom, v);
        }
        JS_FreePropertyEnum(ctx, props, propCount);
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

void JsHttpBinding::attachRuntime(JSRuntime* rt);
void JsHttpBinding::detachRuntime(JSRuntime* rt);
JSModuleDef* JsHttpBinding::initModule(JSContext* ctx, const char* name);
void JsHttpBinding::reset(JSContext* ctx);

// 约束：
// 1) attachRuntime: 创建 runtime 级 HttpState（含 QNetworkAccessManager）
// 2) detachRuntime: 终止并释放所有 pending 请求、timer、JS 回调值
// 3) initModule: 导出 request/get/post 三个符号
// 4) reset: 仅用于测试，清空当前 runtime 状态

} // namespace stdiolink_service
```

### 4.5 测试用 HTTP Stub 服务器

```cpp
// src/tests/helpers/http_test_server.h
#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

/// @brief 本地 HTTP stub 服务器，供测试使用
///
/// 基于 QTcpServer，支持注册路由回调，
/// 避免测试依赖外部网络。
class HttpTestServer : public QTcpServer {
    Q_OBJECT
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray contentType = "text/plain";
        QByteArray body;
    };

    using Handler = std::function<Response(const Request&)>;

    explicit HttpTestServer(QObject* parent = nullptr);

    /// 注册路由处理函数
    void route(const QByteArray& method,
               const QByteArray& path, Handler handler);

    /// 获取服务器 URL（如 "http://127.0.0.1:12345"）
    QString baseUrl() const;

private slots:
    void onNewConnection();

private:
    QMap<QPair<QByteArray,QByteArray>, Handler> m_routes;
};
```

### 4.6 main.cpp 集成

```cpp
#include "bindings/js_http.h"

// 在 engine 创建后：
JsHttpBinding::attachRuntime(engine.runtime());

// 模块注册（在 stdiolink/time 之后）：
engine.registerModule("stdiolink/http", JsHttpBinding::initModule);
```

`JsHttpBinding::detachRuntime(oldRt)` 统一放在 `JsEngine::~JsEngine()` 中执行，避免重复清理。

### 4.7 JS 使用示例

```js
import { request, get, post } from "stdiolink/http";

// GET 请求
const resp = await get("http://api.example.com/data");
console.log(resp.status, resp.bodyJson);

// POST JSON
const result = await post("http://api.example.com/submit", {
    name: "test", value: 42
});

// 自定义请求
const custom = await request({
    method: "PUT",
    url: "http://api.example.com/item/1",
    headers: { "Authorization": "Bearer token" },
    body: { updated: true },
    timeoutMs: 5000
});
```

---

## 5. 实现步骤

1. 新增 `js_http` 绑定并注册模块
2. 实现 `request/get/post` 与请求参数解析
3. 实现超时控制与错误映射
4. 统一响应对象结构
5. 在 `JsEngine::~JsEngine()` 接入 `JsHttpBinding::detachRuntime(oldRt)`
6. 编写本地 HTTP stub 测试
7. 更新 manual 文档

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_http.h`
- `src/stdiolink_service/bindings/js_http.cpp`
- `src/tests/test_http_binding.cpp`
- `src/tests/helpers/http_test_server.h`（本地 stub HTTP 服务器，基于 `QTcpServer`，供测试使用）
- `doc/manual/10-js-service/http-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/engine/js_engine.cpp`
- `src/stdiolink_service/CMakeLists.txt`（增加 Qt Network 依赖）
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

新增测试文件：`test_http_binding.cpp`

### 7.1 测试 Fixture

```cpp
// test_http_binding.cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_http.h"
#include "helpers/http_test_server.h"

using namespace stdiolink_service;

class JsHttpTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        JsHttpBinding::attachRuntime(engine->runtime());
        ModuleLoader::addBuiltin("stdiolink/http",
                                 JsHttpBinding::initModule);

        // 启动本地 stub 服务器
        server = std::make_unique<HttpTestServer>();
        ASSERT_TRUE(server->listen(
            QHostAddress::LocalHost, 0));
        setupRoutes();
    }
    void TearDown() override {
        JsHttpBinding::reset(engine->context());
        JsHttpBinding::detachRuntime(engine->runtime());
        engine.reset();
        server->close();
        server.reset();
    }

    void setupRoutes() {
        // GET /hello → 200 "Hello World"
        server->route("GET", "/hello", [](const auto&) {
            return HttpTestServer::Response{
                200, "text/plain", "Hello World"};
        });
        // GET /json → 200 JSON
        server->route("GET", "/json", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json",
                R"({"key":"value","num":42})"};
        });
        // POST /echo → 回显请求 body
        server->route("POST", "/echo",
            [](const HttpTestServer::Request& req) {
            return HttpTestServer::Response{
                200, "application/json", req.body};
        });
        // GET /not-found → 404
        server->route("GET", "/not-found", [](const auto&) {
            return HttpTestServer::Response{
                404, "text/plain", "Not Found"};
        });
        // GET /server-error → 500
        server->route("GET", "/server-error", [](const auto&) {
            return HttpTestServer::Response{
                500, "text/plain", "Internal Server Error"};
        });
        // GET /bad-json → 200 但 body 非法 JSON
        server->route("GET", "/bad-json", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json", "not valid json {{{"};
        });
        // PUT /item → 200 JSON
        server->route("PUT", "/item", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json",
                R"({"updated":true})"};
        });
        // GET /slow → 延迟 2 秒响应（用于超时测试）
        server->route("GET", "/slow",
            [](const auto&) {
            QThread::msleep(2000);
            return HttpTestServer::Response{
                200, "text/plain", "slow"};
        });
        // GET /headers → 回显请求头
        server->route("GET", "/headers",
            [](const HttpTestServer::Request& req) {
            QJsonObject obj;
            for (auto it = req.headers.begin();
                 it != req.headers.end(); ++it) {
                obj[QString::fromUtf8(it.key())] =
                    QString::fromUtf8(it.value());
            }
            QJsonDocument doc(obj);
            return HttpTestServer::Response{
                200, "application/json",
                doc.toJson(QJsonDocument::Compact)};
        });
    }

    int runScript(const QString& code) {
        QString wrapped = QString(
            "globalThis.__baseUrl = '%1';\n%2"
        ).arg(server->baseUrl(), code);
        QTemporaryDir dir;
        QString path = dir.path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(wrapped.toUtf8());
        f.close();
        int ret = engine->evalFile(path);
        // 驱动 pending jobs（HTTP Promise 回调）
        int maxIter = 500;
        while (engine->hasPendingJobs() && maxIter-- > 0) {
            QCoreApplication::processEvents(
                QEventLoop::AllEvents, 50);
            engine->executePendingJobs();
        }
        return ret;
    }

    int32_t getGlobalInt(const char* name) {
        JSValue g = JS_GetGlobalObject(engine->context());
        JSValue v = JS_GetPropertyStr(engine->context(), g, name);
        int32_t r = 0;
        JS_ToInt32(engine->context(), &r, v);
        JS_FreeValue(engine->context(), v);
        JS_FreeValue(engine->context(), g);
        return r;
    }

    std::unique_ptr<JsEngine> engine;
    std::unique_ptr<HttpTestServer> server;
};
```

### 7.2 正常请求

```cpp
TEST_F(JsHttpTest, GetReturns200AndBody) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/hello');\n"
        "globalThis.ok = (resp.status === 200"
        " && resp.bodyText === 'Hello World') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, GetJsonAutoParses) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/json');\n"
        "globalThis.ok = (resp.status === 200"
        " && resp.bodyJson.key === 'value'"
        " && resp.bodyJson.num === 42) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, PostJsonEcho) {
    int ret = runScript(
        "import { post } from 'stdiolink/http';\n"
        "const resp = await post(__baseUrl + '/echo',"
        " { name: 'test', value: 42 });\n"
        "globalThis.ok = (resp.status === 200"
        " && resp.bodyJson.name === 'test'"
        " && resp.bodyJson.value === 42) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, CustomMethodPut) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "const resp = await request({\n"
        "  method: 'PUT', url: __baseUrl + '/item',\n"
        "  body: { updated: true }\n"
        "});\n"
        "globalThis.ok = (resp.status === 200"
        " && resp.bodyJson.updated === true) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, ObjectBodySetsContentTypeJson) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "const resp = await request({\n"
        "  method: 'POST', url: __baseUrl + '/echo',\n"
        "  body: { a: 1 }\n"
        "});\n"
        "// 服务端收到的 body 应为合法 JSON\n"
        "globalThis.ok = (resp.bodyJson.a === 1) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, EmptyBodyGetSucceeds) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/hello');\n"
        "globalThis.ok = (resp.status === 200) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.3 参数与头部

```cpp
TEST_F(JsHttpTest, CustomHeadersPassedThrough) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "const resp = await request({\n"
        "  method: 'GET', url: __baseUrl + '/headers',\n"
        "  headers: { 'X-Custom': 'test-value' }\n"
        "});\n"
        "globalThis.ok = (resp.bodyJson['x-custom']"
        " === 'test-value') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, QueryParamsEncoded) {
    // 需要服务端路由支持 query 回显
    server->route("GET", "/query",
        [](const HttpTestServer::Request& req) {
        return HttpTestServer::Response{
            200, "text/plain", req.path};
    });
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "const resp = await request({\n"
        "  method: 'GET', url: __baseUrl + '/query',\n"
        "  query: { key: 'hello world', num: '42' }\n"
        "});\n"
        "globalThis.ok = (resp.bodyText.includes('key=hello')"
        " && resp.bodyText.includes('num=42')) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, TimeoutRejects) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "try {\n"
        "  await request({\n"
        "    method: 'GET', url: __baseUrl + '/slow',\n"
        "    timeoutMs: 100\n"
        "  });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.4 错误与边界

```cpp
TEST_F(JsHttpTest, InvalidUrlThrows) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "try {\n"
        "  await request({ url: 'not a url' });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, NetworkUnreachableRejects) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "try {\n"
        "  await request({\n"
        "    url: 'http://127.0.0.1:1',\n"
        "    timeoutMs: 2000\n"
        "  });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, Http404ResolvesWithStatus) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/not-found');\n"
        "globalThis.ok = (resp.status === 404) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, Http500ResolvesWithStatus) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/server-error');\n"
        "globalThis.ok = (resp.status === 500) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, ParseJsonTrueWithBadJsonRejects) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "try {\n"
        "  await request({\n"
        "    url: __baseUrl + '/bad-json',\n"
        "    parseJson: true\n"
        "  });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsHttpTest, MissingUrlThrowsTypeError) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "try {\n"
        "  await request({ method: 'GET' });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e instanceof TypeError) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.5 并发与稳定性

```cpp
TEST_F(JsHttpTest, ConcurrentRequestsAllResolve) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const urls = Array.from({length: 5},"
        " () => __baseUrl + '/hello');\n"
        "const results = await Promise.all("
        "urls.map(u => get(u)));\n"
        "globalThis.ok = results.every("
        "r => r.status === 200) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.6 回归测试

- 全量 JS 绑定核心测试无回归

---

## 8. 验收标准（DoD）

- HTTP API 与文档一致
- transport/error/status 语义清晰且测试覆盖
- 新增与回归测试全部通过

---

## 9. 风险与控制

- **风险 1**：请求生命周期与 JS Promise 生命周期不一致
  - 控制：reply 与 promise 绑定，统一清理路径
- **风险 2**：测试网络环境不稳定
  - 控制：测试全部使用本地 stub server，避免外网依赖

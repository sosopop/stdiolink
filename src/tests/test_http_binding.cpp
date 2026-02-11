#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_http.h"
#include "helpers/http_test_server.h"

using namespace stdiolink_service;

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& name, const QString& content) {
    QString path = dir.path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream out(&f);
    out << content;
    out.flush();
    return path;
}

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue v = JS_GetPropertyStr(ctx, g, key);
    int32_t r = 0;
    JS_ToInt32(ctx, &r, v);
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, g);
    return r;
}

} // namespace

class JsHttpTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        JsHttpBinding::attachRuntime(m_engine->runtime());
        m_engine->registerModule("stdiolink/http", JsHttpBinding::initModule);

        m_server = std::make_unique<HttpTestServer>();
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            GTEST_SKIP() << "HttpTestServer failed to listen on localhost";
        }
        setupRoutes();
    }

    void TearDown() override {
        JsHttpBinding::reset(m_engine->context());
        m_engine.reset();
        if (m_server) {
            m_server->close();
            m_server.reset();
        }
    }

    void setupRoutes() {
        m_server->route("GET", "/hello", [](const auto&) {
            return HttpTestServer::Response{200, "text/plain", "Hello World"};
        });
        m_server->route("GET", "/json", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json", R"({"key":"value","num":42})"};
        });
        m_server->route("POST", "/echo", [](const HttpTestServer::Request& req) {
            return HttpTestServer::Response{200, "application/json", req.body};
        });
        m_server->route("GET", "/not-found", [](const auto&) {
            return HttpTestServer::Response{404, "text/plain", "Not Found"};
        });
        m_server->route("GET", "/server-error", [](const auto&) {
            return HttpTestServer::Response{500, "text/plain", "Internal Server Error"};
        });
        m_server->route("GET", "/bad-json", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json", "not valid json {{{"};
        });
        m_server->route("PUT", "/item", [](const auto&) {
            return HttpTestServer::Response{
                200, "application/json", R"({"updated":true})"};
        });
        m_server->route("GET", "/slow", [](const auto&) {
            return HttpTestServer::Response{200, "text/plain", "slow", 2000};
        });
        m_server->route("GET", "/headers", [](const HttpTestServer::Request& req) {
            QByteArray json = "{";
            bool first = true;
            for (auto it = req.headers.begin(); it != req.headers.end(); ++it) {
                if (!first) json += ",";
                json += "\"" + it.key() + "\":\"" + it.value() + "\"";
                first = false;
            }
            json += "}";
            return HttpTestServer::Response{200, "application/json", json};
        });
        m_server->route("GET", "/query", [](const HttpTestServer::Request& req) {
            return HttpTestServer::Response{200, "text/plain", req.path};
        });
    }

    int runScript(const QString& code) {
        QString wrapped = QString(
            "globalThis.__baseUrl = '%1';\n%2"
        ).arg(m_server->baseUrl(), code);
        QString path = writeScript(m_tmpDir, "test.mjs", wrapped);
        EXPECT_FALSE(path.isEmpty());
        int ret = m_engine->evalFile(path);
        // Drive both Qt event loop (for QNetworkReply) and QuickJS jobs.
        // WaitForMoreEvents ensures we actually block until timers/IO fire.
        for (int i = 0; i < 500; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (m_engine->hasPendingJobs()) {
                m_engine->executePendingJobs();
            }
            if (!JsHttpBinding::hasPending(m_engine->context())
                && !m_engine->hasPendingJobs()) {
                break;
            }
            QThread::msleep(5);
        }
        return ret;
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
    std::unique_ptr<HttpTestServer> m_server;
};

// ── Normal Requests ──

TEST_F(JsHttpTest, GetReturns200AndBody) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/hello');\n"
        "globalThis.ok = (resp.status === 200"
        " && resp.bodyText === 'Hello World') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Headers & Query ──

TEST_F(JsHttpTest, CustomHeadersPassedThrough) {
    int ret = runScript(
        "import { request } from 'stdiolink/http';\n"
        "const resp = await request({\n"
        "  method: 'GET', url: __baseUrl + '/headers',\n"
        "  headers: { 'x-custom': 'test-value' }\n"
        "});\n"
        "globalThis.ok = (resp.bodyJson['x-custom']"
        " === 'test-value') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsHttpTest, QueryParamsEncoded) {
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Error & Edge Cases ──

TEST_F(JsHttpTest, Http404ResolvesWithStatus) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/not-found');\n"
        "globalThis.ok = (resp.status === 404) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsHttpTest, Http500ResolvesWithStatus) {
    int ret = runScript(
        "import { get } from 'stdiolink/http';\n"
        "const resp = await get(__baseUrl + '/server-error');\n"
        "globalThis.ok = (resp.status === 500) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsHttpTest, ParseJsonWithBadJsonRejects) {
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Concurrency ──

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
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QHttpServer>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>

#include "stdiolink/host/driver_catalog.h"
#include "stdiolink_server/http/driverlab_ws_connection.h"
#include "stdiolink_server/http/driverlab_ws_handler.h"

using namespace stdiolink_server;

// ---------------------------------------------------------------------------
// parseConnectionParams (static, no server needed)
// ---------------------------------------------------------------------------

TEST(DriverLabWsHandlerTest, ParseConnectionParamsBasic) {
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/driver_modbustcp");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_EQ(params.driverId, "driver_modbustcp");
    EXPECT_EQ(params.runMode, "oneshot");  // default
    EXPECT_TRUE(params.extraArgs.isEmpty());
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsWithRunMode) {
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/my_driver?runMode=keepalive");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_EQ(params.driverId, "my_driver");
    EXPECT_EQ(params.runMode, "keepalive");
    EXPECT_TRUE(params.extraArgs.isEmpty());
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsWithArgs) {
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/drv1?runMode=oneshot&args=--verbose,--port%3D502");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_EQ(params.driverId, "drv1");
    EXPECT_EQ(params.runMode, "oneshot");
    ASSERT_EQ(params.extraArgs.size(), 2);
    EXPECT_EQ(params.extraArgs[0], "--verbose");
    EXPECT_EQ(params.extraArgs[1], "--port=502");
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsInvalidPath) {
    const QUrl url("ws://127.0.0.1:8080/api/other/something");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_TRUE(params.driverId.isEmpty());
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsEmptyDriverId) {
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_TRUE(params.driverId.isEmpty());
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsDefaultRunMode) {
    // No runMode query param → defaults to "oneshot"
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/test_drv");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_EQ(params.runMode, "oneshot");
}

TEST(DriverLabWsHandlerTest, ParseConnectionParamsEmptyArgs) {
    const QUrl url("ws://127.0.0.1:8080/api/driverlab/drv?args=");
    const auto params = DriverLabWsHandler::parseConnectionParams(url);
    EXPECT_TRUE(params.extraArgs.isEmpty());
}

// ---------------------------------------------------------------------------
// Handler construction & initial state
// ---------------------------------------------------------------------------

TEST(DriverLabWsHandlerTest, InitialConnectionCountIsZero) {
    stdiolink::DriverCatalog catalog;
    DriverLabWsHandler handler(&catalog);
    EXPECT_EQ(handler.activeConnectionCount(), 0);
}

TEST(DriverLabWsHandlerTest, CloseAllOnEmptyIsNoOp) {
    stdiolink::DriverCatalog catalog;
    DriverLabWsHandler handler(&catalog);
    handler.closeAll();
    EXPECT_EQ(handler.activeConnectionCount(), 0);
}

// ---------------------------------------------------------------------------
// WebSocket verifier integration tests (real QHttpServer + QWebSocket client)
// ---------------------------------------------------------------------------

namespace {

QString exeSuffix() {
#ifdef Q_OS_WIN
    return ".exe";
#else
    return QString();
#endif
}

QString testBinaryPath(const QString& baseName) {
    return QCoreApplication::applicationDirPath() + "/" + baseName + exeSuffix();
}

struct WsTestFixture {
    stdiolink::DriverCatalog catalog;
    QHttpServer httpServer;
    QTcpServer tcpServer;
    DriverLabWsHandler* handler = nullptr;
    quint16 port = 0;

    bool setup(const QString& program = QString()) {
        QHash<QString, stdiolink::DriverConfig> drivers;
        stdiolink::DriverConfig cfg;
        cfg.id = "test_driver";
        cfg.program = program.isEmpty() ? testBinaryPath("test_driver") : program;
        drivers.insert(cfg.id, cfg);
        catalog.replaceAll(drivers);

        handler = new DriverLabWsHandler(&catalog);
        handler->registerVerifier(httpServer);

        if (!tcpServer.listen(QHostAddress::LocalHost, 0)) {
            return false;
        }
        if (!httpServer.bind(&tcpServer)) {
            return false;
        }
        port = tcpServer.serverPort();
        return true;
    }

    ~WsTestFixture() {
        delete handler;
    }
};

// Helper: attempt WebSocket connection and wait for connected or error.
// Collects all text messages received during and after handshake.
// Returns true if connected, false if rejected/error.
bool attemptWsConnect(QWebSocket& ws, const QUrl& url,
                      QStringList& receivedMessages,
                      QWebSocketProtocol::CloseCode& closeCode,
                      int timeoutMs = 3000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool connected = false;

    // Connect message handler BEFORE open() so we don't miss early messages
    QObject::connect(&ws, &QWebSocket::textMessageReceived,
                     [&receivedMessages](const QString& msg) {
        receivedMessages.append(msg);
    });

    QObject::connect(&ws, &QWebSocket::connected, &loop, [&]() {
        connected = true;
        loop.quit();
    });
    QObject::connect(&ws, &QWebSocket::disconnected, &loop, [&]() {
        closeCode = ws.closeCode();
        loop.quit();
    });
    QObject::connect(&ws, &QWebSocket::errorOccurred, &loop, [&]() {
        loop.quit();
    });

    ws.open(url);
    timeout.start(timeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    return connected;
}

void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

TEST(DriverLabWsHandlerTest, VerifierRejectsUnknownDriver) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/nonexistent_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    EXPECT_FALSE(connected);
}

TEST(DriverLabWsHandlerTest, VerifierRejectsInvalidRunMode) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver?runMode=invalid").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    EXPECT_FALSE(connected);
}

TEST(DriverLabWsHandlerTest, VerifierAcceptsValidDriver) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    EXPECT_TRUE(connected);

    if (connected) {
        ws.close();
        waitMs(200);
    }
}

TEST(DriverLabWsHandlerTest, VerifierAcceptsKeepaliveMode) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver?runMode=keepalive").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    EXPECT_TRUE(connected);

    if (connected) {
        ws.close();
        waitMs(200);
    }
}

TEST(DriverLabWsHandlerTest, ConnectionCountIncrementsOnConnect) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    // cat stays alive on stdin, so the connection should persist
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(300);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);
}

TEST(DriverLabWsHandlerTest, DriverStartedMessageReceived) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    // Wait for driver.started message (cat starts quickly)
    waitMs(500);

    // Should have received at least driver.started
    ASSERT_FALSE(messages.isEmpty());

    // First message should be driver.started
    const QJsonDocument doc = QJsonDocument::fromJson(messages.first().toUtf8());
    ASSERT_TRUE(doc.isObject());
    const QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("type").toString(), "driver.started");
    EXPECT_TRUE(obj.contains("pid"));

    ws.close();
    waitMs(200);
}

TEST(DriverLabWsHandlerTest, InvalidJsonMessageReturnsError) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    waitMs(300);
    messages.clear();  // discard startup messages

    ws.sendTextMessage("not valid json{{{");
    waitMs(500);

    // Should have received an error response
    bool gotError = false;
    for (const QString& msg : messages) {
        const QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
        if (doc.isObject() && doc.object().value("type").toString() == "error") {
            gotError = true;
            break;
        }
    }
    EXPECT_TRUE(gotError);

    ws.close();
    waitMs(200);
}

TEST(DriverLabWsHandlerTest, UnknownMessageTypeReturnsError) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver").arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    waitMs(300);
    messages.clear();

    ws.sendTextMessage(R"({"type":"unknown_type"})");
    waitMs(500);

    bool gotError = false;
    for (const QString& msg : messages) {
        const QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
        if (doc.isObject() && doc.object().value("type").toString() == "error") {
            EXPECT_TRUE(doc.object().value("message").toString().contains("unknown"));
            gotError = true;
            break;
        }
    }
    EXPECT_TRUE(gotError);

    ws.close();
    waitMs(200);
}

// ---------------------------------------------------------------------------
// Heartbeat tests (M75: WebSocket Ping/Pong)
// ---------------------------------------------------------------------------

// T01 — Heartbeat constants
TEST(DriverLabWsHandlerTest, HeartbeatConstants) {
    EXPECT_EQ(DriverLabWsHandler::kPingIntervalMs, 30000);
    EXPECT_EQ(DriverLabWsHandler::kPongTimeoutMs, 60000);
    EXPECT_EQ(DriverLabWsHandler::kPongTimeoutMs,
              DriverLabWsHandler::kPingIntervalMs * 2);
}

// T02 — Server ping → client auto-pong → server onPongReceived updates lastPongAt
TEST(DriverLabWsHandlerTest, PongUpdatesLastPongAt) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);  // wait for driver.started

    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    const QDateTime t0 = conn->lastPongAt();

    // Wait for timer to fire → server sends ping → client auto-replies pong
    // → server onPongReceived updates lastPongAt
    waitMs(300);

    EXPECT_GT(conn->lastPongAt(), t0);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}

// T03 — Healthy connection survives multiple sweep cycles
TEST(DriverLabWsHandlerTest, HealthyConnectionSurvivesSweep) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);

    // Wait for multiple timer ticks (sweep + ping cycles)
    // Client auto-replies pong, so lastPongAt stays fresh → sweep skips
    waitMs(300);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}

// T04 — New connection not swept immediately
TEST(DriverLabWsHandlerTest, NewConnectionNotSweptImmediately) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    // Wait for at least one timer tick → sweep runs on fresh connection
    // m_lastPongAt ≈ now → elapsed ≈ 0 → well within 60s → survives
    waitMs(150);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}

// T05 — Empty connection list: timer tick is no-op, no crash
TEST(DriverLabWsHandlerTest, PingTickWithNoConnectionsIsNoOp) {
    stdiolink::DriverCatalog catalog;
    DriverLabWsHandler handler(&catalog);

    handler.setPingIntervalForTest(50);

    EXPECT_EQ(handler.activeConnectionCount(), 0);
    waitMs(200);  // several ticks fire on empty list
    EXPECT_EQ(handler.activeConnectionCount(), 0);
}

// T06 — Timeout connection swept with CloseCodeGoingAway, quota released
TEST(DriverLabWsHandlerTest, TimeoutConnectionSweptWithGoingAway) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    // Set up a persistent disconnected listener to capture close code
    QWebSocketProtocol::CloseCode sweepCloseCode = QWebSocketProtocol::CloseCodeNormal;
    bool disconnected = false;
    QObject::connect(&ws, &QWebSocket::disconnected, [&]() {
        sweepCloseCode = ws.closeCode();
        disconnected = true;
    });

    // Simulate pong timeout: set lastPongAt to 61 seconds ago
    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    conn->setLastPongAtForTest(
        QDateTime::currentDateTimeUtc().addMSecs(-61000));

    // Wait for next timer tick → sweep detects timeout → closeForPongTimeout
    waitMs(300);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);
    EXPECT_TRUE(disconnected);
    EXPECT_EQ(sweepCloseCode, QWebSocketProtocol::CloseCodeGoingAway);
    EXPECT_EQ(ws.closeReason(), QStringLiteral("pong timeout"));

    // Verify quota released: a new client can connect successfully
    QWebSocket ws2;
    QStringList msg2;
    QWebSocketProtocol::CloseCode cc2 = QWebSocketProtocol::CloseCodeNormal;
    const bool reconnected = attemptWsConnect(ws2, url, msg2, cc2);
    ASSERT_TRUE(reconnected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws2.close();
    waitMs(200);
}

// T07 — closeAll clears connections; new connections restore heartbeat
TEST(DriverLabWsHandlerTest, CloseAllClearsAndHeartbeatRecovers) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws1, ws2;
    QStringList msg1, msg2;
    QWebSocketProtocol::CloseCode cc1 = QWebSocketProtocol::CloseCodeNormal;
    QWebSocketProtocol::CloseCode cc2 = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    attemptWsConnect(ws1, url, msg1, cc1);
    attemptWsConnect(ws2, url, msg2, cc2);
    waitMs(300);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 2);

    // closeAll: stop timer + delete all connections
    fixture.handler->closeAll();
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);

    // Connect a new client after closeAll — timer should auto-restart,
    // so heartbeat protection is restored for the new connection.
    QWebSocket ws3;
    QStringList msg3;
    QWebSocketProtocol::CloseCode cc3 = QWebSocketProtocol::CloseCodeNormal;
    const bool connected = attemptWsConnect(ws3, url, msg3, cc3);
    ASSERT_TRUE(connected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    // Simulate pong timeout on the new connection
    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    conn->setLastPongAtForTest(
        QDateTime::currentDateTimeUtc().addMSecs(-61000));

    // Timer restarted → sweep fires → expired connection is cleaned up
    waitMs(300);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);
}

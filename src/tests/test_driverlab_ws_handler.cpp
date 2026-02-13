#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHttpServer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebSocket>

#include "stdiolink/host/driver_catalog.h"
#include "stdiolink_server/http/driverlab_ws_connection.h"
#include "stdiolink_server/http/driverlab_ws_handler.h"

using namespace stdiolink;
using namespace stdiolink_server;

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

// Helper: set up a DriverCatalog with a test driver
void setupCatalog(DriverCatalog& catalog) {
    DriverConfig cfg;
    cfg.id = "test_driver";
    cfg.program = testBinaryPath("test_driver");
    QHash<QString, DriverConfig> drivers;
    drivers.insert(cfg.id, cfg);
    catalog.replaceAll(drivers);
}

// Helper: set up a DriverCatalog with a meta-capable test driver
void setupCatalogWithMetaDriver(DriverCatalog& catalog) {
    DriverConfig cfg;
    cfg.id = "test_meta_driver";
    cfg.program = testBinaryPath("test_meta_driver");
    QHash<QString, DriverConfig> drivers;
    drivers.insert(cfg.id, cfg);
    catalog.replaceAll(drivers);
}

struct WsTestEnv {
    std::unique_ptr<QHttpServer> server;
    std::unique_ptr<QTcpServer> tcpServer;
    std::unique_ptr<DriverLabWsHandler> handler;
    DriverCatalog catalog;
    quint16 port = 0;

    bool setup(bool useMeta = false) {
        if (useMeta)
            setupCatalogWithMetaDriver(catalog);
        else
            setupCatalog(catalog);

        server = std::make_unique<QHttpServer>();
        tcpServer = std::make_unique<QTcpServer>();
        handler = std::make_unique<DriverLabWsHandler>(&catalog);

        handler->registerVerifier(*server);

        if (!tcpServer->listen(QHostAddress::LocalHost, 0))
            return false;
        if (!server->bind(tcpServer.get()))
            return false;

        port = tcpServer->serverPort();
        return true;
    }

    QString wsUrl(const QString& path) const {
        return QString("ws://127.0.0.1:%1%2").arg(port).arg(path);
    }
};

// Helper: connect a WebSocket and wait for connection or error
// Returns true if connected, false on error/timeout
bool connectWs(QWebSocket& ws, const QString& url, int timeoutMs = 3000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool connected = false;

    QObject::connect(&ws, &QWebSocket::connected, &loop, [&]() {
        connected = true;
        loop.quit();
    });
    QObject::connect(&ws, &QWebSocket::errorOccurred, &loop, [&]() { loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    ws.open(QUrl(url));
    timeout.start(timeoutMs);
    loop.exec();

    return connected;
}

// Helper: wait for a text message on a WebSocket
QString waitForMessage(QWebSocket& ws, int timeoutMs = 3000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QString result;

    QObject::connect(&ws, &QWebSocket::textMessageReceived, &loop, [&](const QString& msg) {
        result = msg;
        loop.quit();
    });
    QObject::connect(&ws, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(timeoutMs);
    loop.exec();

    return result;
}

// Helper: wait for disconnect
bool waitForDisconnect(QWebSocket& ws, int timeoutMs = 5000) {
    if (ws.state() == QAbstractSocket::UnconnectedState)
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool disconnected = false;

    QObject::connect(&ws, &QWebSocket::disconnected, &loop, [&]() {
        disconnected = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(timeoutMs);
    loop.exec();

    return disconnected;
}

// Helper: collect multiple messages until timeout or count reached
QStringList collectMessages(QWebSocket& ws, int count, int timeoutMs = 5000) {
    QStringList messages;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&ws, &QWebSocket::textMessageReceived, &loop, [&](const QString& msg) {
        messages.append(msg);
        if (messages.size() >= count)
            loop.quit();
    });
    QObject::connect(&ws, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(timeoutMs);
    loop.exec();

    return messages;
}

QJsonObject parseMsg(const QString& msg) {
    return QJsonDocument::fromJson(msg.toUtf8()).object();
}

// Helper: find a message with a specific type in a list
bool hasMessageType(const QStringList& msgs, const QString& type) {
    for (const auto& m : msgs) {
        if (parseMsg(m)["type"].toString() == type)
            return true;
    }
    return false;
}

QJsonObject findMessageByType(const QStringList& msgs, const QString& type) {
    for (const auto& m : msgs) {
        QJsonObject obj = parseMsg(m);
        if (obj["type"].toString() == type)
            return obj;
    }
    return {};
}

} // namespace

// --- DriverLabWsHandler verifier tests (via WebSocket connection attempts) ---

TEST(DriverLabWsHandlerTest, AcceptValidDriver) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=oneshot")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 1);
    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsHandlerTest, DenyUnknownDriver) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    EXPECT_FALSE(connectWs(ws, env.wsUrl("/api/driverlab/nonexistent_driver")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 0);
}

TEST(DriverLabWsHandlerTest, DenyInvalidRunMode) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    EXPECT_FALSE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=invalid")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 0);
}

TEST(DriverLabWsHandlerTest, DefaultRunModeIsOneshot) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 1);
    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsHandlerTest, ConnectionCountDecreasesOnDisconnect) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 1);

    ws.close();
    waitForDisconnect(ws);

    // Process events to let the handler clean up
    QCoreApplication::processEvents();
    QTimer::singleShot(200, []() { QCoreApplication::processEvents(); });
    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(env.handler->activeConnectionCount(), 0);
}

TEST(DriverLabWsHandlerTest, DenyWhenMaxConnectionsReached) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    // Fill up to max connections
    std::vector<std::unique_ptr<QWebSocket>> sockets;
    for (int i = 0; i < DriverLabWsHandler::kMaxConnections; ++i) {
        auto ws = std::make_unique<QWebSocket>();
        if (!connectWs(*ws, env.wsUrl("/api/driverlab/test_driver"))) {
            GTEST_SKIP() << "Could not establish connection " << i;
        }
        sockets.push_back(std::move(ws));
    }
    EXPECT_EQ(env.handler->activeConnectionCount(), DriverLabWsHandler::kMaxConnections);

    // Next connection should be denied
    QWebSocket extraWs;
    EXPECT_FALSE(connectWs(extraWs, env.wsUrl("/api/driverlab/test_driver")));

    // Clean up
    for (auto& ws : sockets) {
        ws->close();
    }
}

TEST(DriverLabWsHandlerTest, CloseAllTerminatesConnections) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws1, ws2;
    ASSERT_TRUE(connectWs(ws1, env.wsUrl("/api/driverlab/test_driver")));
    ASSERT_TRUE(connectWs(ws2, env.wsUrl("/api/driverlab/test_driver")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 2);

    env.handler->closeAll();
    EXPECT_EQ(env.handler->activeConnectionCount(), 0);
}

// --- DriverLabWsConnection tests (via WebSocket messages) ---

TEST(DriverLabWsConnectionTest, DriverStartedMessageOnConnect) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver")));

    // Collect initial messages (driver.started + stdout from meta error response)
    auto msgs = collectMessages(ws, 2, 3000);
    ASSERT_FALSE(msgs.isEmpty()) << "No messages received";

    QJsonObject started = findMessageByType(msgs, "driver.started");
    EXPECT_FALSE(started.isEmpty()) << "Expected driver.started message";
    EXPECT_TRUE(started.contains("pid"));
    EXPECT_GT(started["pid"].toInteger(), 0);

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, ExecForwardsToDriverStdin) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=keepalive")));

    // Consume initial messages (driver.started + stdout from meta error)
    auto initial = collectMessages(ws, 2, 3000);
    ASSERT_FALSE(initial.isEmpty());

    // Send exec command (echo)
    QJsonObject execMsg;
    execMsg["type"] = "exec";
    execMsg["cmd"] = "echo";
    execMsg["data"] = QJsonObject{{"hello", "world"}};
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(execMsg).toJson(QJsonDocument::Compact)));

    // Wait for stdout response
    QString response = waitForMessage(ws, 3000);
    ASSERT_FALSE(response.isEmpty()) << "No response from driver";
    QJsonObject respObj = parseMsg(response);
    EXPECT_EQ(respObj["type"].toString(), "stdout");

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, InvalidJsonSendsError) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver")));

    // Consume initial messages (driver.started + stdout from meta error)
    collectMessages(ws, 2, 2000);

    // Send invalid JSON
    ws.sendTextMessage("not valid json{{{");

    QString msg = waitForMessage(ws, 2000);
    ASSERT_FALSE(msg.isEmpty());
    QJsonObject obj = parseMsg(msg);
    EXPECT_EQ(obj["type"].toString(), "error");
    EXPECT_TRUE(obj["message"].toString().contains("invalid JSON"));

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, UnknownTypeSendsError) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver")));

    // Consume initial messages (driver.started + stdout from meta error)
    collectMessages(ws, 2, 2000);

    // Send unknown type
    QJsonObject unknownMsg;
    unknownMsg["type"] = "foobar";
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(unknownMsg).toJson(QJsonDocument::Compact)));

    QString msg = waitForMessage(ws, 2000);
    ASSERT_FALSE(msg.isEmpty());
    QJsonObject obj = parseMsg(msg);
    EXPECT_EQ(obj["type"].toString(), "error");
    EXPECT_TRUE(obj["message"].toString().contains("unknown message type"));

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, KeepAliveDriverExitClosesWebSocket) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=keepalive")));

    // Consume initial messages (driver.started + stdout from meta error)
    auto initMsgs = collectMessages(ws, 2, 2000);

    // Send exit_now command to make driver exit
    QJsonObject execMsg;
    execMsg["type"] = "exec";
    execMsg["cmd"] = "exit_now";
    execMsg["data"] = QJsonObject();
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(execMsg).toJson(QJsonDocument::Compact)));

    // Should receive driver.exited then WebSocket should close
    auto msgs = collectMessages(ws, 3, 5000);

    // Find driver.exited message
    bool foundExited = false;
    for (const auto& m : msgs) {
        QJsonObject obj = parseMsg(m);
        if (obj["type"].toString() == "driver.exited") {
            foundExited = true;
            EXPECT_TRUE(obj.contains("exitCode"));
            EXPECT_TRUE(obj.contains("exitStatus"));
            break;
        }
    }
    EXPECT_TRUE(foundExited) << "Expected driver.exited message";

    // WebSocket should be closed by server
    EXPECT_TRUE(waitForDisconnect(ws, 3000));
}

TEST(DriverLabWsConnectionTest, OneShotDriverExitKeepsWebSocketOpen) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=oneshot")));

    // Consume initial messages (driver.started + stdout from meta error)
    collectMessages(ws, 2, 2000);

    // Send exit_now command to make driver exit
    QJsonObject execMsg;
    execMsg["type"] = "exec";
    execMsg["cmd"] = "exit_now";
    execMsg["data"] = QJsonObject();
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(execMsg).toJson(QJsonDocument::Compact)));

    // Should receive driver.exited but WebSocket stays open
    auto msgs = collectMessages(ws, 3, 3000);

    bool foundExited = false;
    for (const auto& m : msgs) {
        QJsonObject obj = parseMsg(m);
        if (obj["type"].toString() == "driver.exited") {
            foundExited = true;
            break;
        }
    }
    EXPECT_TRUE(foundExited) << "Expected driver.exited message";

    // WebSocket should still be open
    EXPECT_EQ(ws.state(), QAbstractSocket::ConnectedState);

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, OneShotAutoRestartOnExec) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=oneshot")));

    // Consume initial messages (driver.started + stdout from meta error)
    collectMessages(ws, 2, 2000);

    // Make driver exit
    QJsonObject exitMsg;
    exitMsg["type"] = "exec";
    exitMsg["cmd"] = "exit_now";
    exitMsg["data"] = QJsonObject();
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(exitMsg).toJson(QJsonDocument::Compact)));

    // Wait for driver.exited
    collectMessages(ws, 3, 3000);

    // Send new exec — should trigger auto-restart
    QJsonObject echoMsg;
    echoMsg["type"] = "exec";
    echoMsg["cmd"] = "echo";
    echoMsg["data"] = QJsonObject{{"test", true}};
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(echoMsg).toJson(QJsonDocument::Compact)));

    // Should get driver.started (from restart) and then driver.restarted and stdout
    auto msgs = collectMessages(ws, 5, 5000);

    bool foundRestarted = false;
    bool foundStarted = false;
    for (const auto& m : msgs) {
        QJsonObject obj = parseMsg(m);
        if (obj["type"].toString() == "driver.restarted") {
            foundRestarted = true;
            EXPECT_TRUE(obj.contains("pid"));
        }
        if (obj["type"].toString() == "driver.started") {
            foundStarted = true;
        }
    }
    EXPECT_TRUE(foundStarted || foundRestarted)
        << "Expected driver.started or driver.restarted after oneshot auto-restart";

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, CancelClosesStdin) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=keepalive")));

    // Consume initial messages (driver.started + stdout from meta error)
    collectMessages(ws, 2, 2000);

    // Send cancel
    QJsonObject cancelMsg;
    cancelMsg["type"] = "cancel";
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(cancelMsg).toJson(QJsonDocument::Compact)));

    // Driver should exit after stdin is closed (keepalive → close WS)
    EXPECT_TRUE(waitForDisconnect(ws, 5000));
}

TEST(DriverLabWsConnectionTest, MetaQueryWithMetaDriver) {
    WsTestEnv env;
    if (!env.setup(true)) // use meta driver
        GTEST_SKIP() << "Cannot set up test server";

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, env.wsUrl("/api/driverlab/test_meta_driver?runMode=keepalive")));

    // Collect initial messages: driver.started + meta
    auto msgs = collectMessages(ws, 3, 5000);

    bool foundStarted = false;
    bool foundMeta = false;
    for (const auto& m : msgs) {
        QJsonObject obj = parseMsg(m);
        if (obj["type"].toString() == "driver.started") {
            foundStarted = true;
        }
        if (obj["type"].toString() == "meta") {
            foundMeta = true;
            EXPECT_EQ(obj["driverId"].toString(), "test_meta_driver");
            EXPECT_EQ(obj["runMode"].toString(), "keepalive");
            EXPECT_TRUE(obj.contains("pid"));
            EXPECT_TRUE(obj.contains("meta"));
        }
    }
    EXPECT_TRUE(foundStarted) << "Expected driver.started message";
    EXPECT_TRUE(foundMeta) << "Expected meta message";

    ws.close();
    waitForDisconnect(ws);
}

TEST(DriverLabWsConnectionTest, ParseConnectionParamsWithArgs) {
    WsTestEnv env;
    if (!env.setup())
        GTEST_SKIP() << "Cannot set up test server";

    // Just verify the connection works with args parameter
    QWebSocket ws;
    ASSERT_TRUE(
        connectWs(ws, env.wsUrl("/api/driverlab/test_driver?runMode=oneshot&args=--verbose")));
    EXPECT_EQ(env.handler->activeConnectionCount(), 1);

    ws.close();
    waitForDisconnect(ws);
}

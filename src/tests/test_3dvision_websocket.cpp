/**
 * M96 — driver_3dvision WebSocket 接口修复测试
 *
 * 白盒层：直接测试 WebSocketClient 公共接口
 * 使用本地 QWebSocketServer 作为 mock server
 */

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include <gtest/gtest.h>

#include "drivers/driver_3dvision/websocket_client.h"

class WebSocketClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 启动本地 mock WebSocket server
        m_server = new QWebSocketServer("MockServer", QWebSocketServer::NonSecureMode);
        ASSERT_TRUE(m_server->listen(QHostAddress::LocalHost, 0));
        m_port = m_server->serverPort();
        m_url = QString("ws://127.0.0.1:%1").arg(m_port);
    }

    void TearDown() override {
        m_server->close();
        delete m_server;
    }

    QWebSocketServer* m_server = nullptr;
    quint16 m_port = 0;
    QString m_url;
};

// T02 — ws.connect 同步等待握手成功
TEST_F(WebSocketClientTest, T02_ConnectWaitsForHandshake) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        Q_UNUSED(sock)
    });

    WebSocketClient client;
    EXPECT_FALSE(client.isConnected());

    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    EXPECT_TRUE(connSpy.wait(3000));
    EXPECT_TRUE(client.isConnected());
}

// T03 — 连接到不监听端口时 error 信号触发
TEST_F(WebSocketClientTest, T03_ConnectFailureEmitsError) {
    m_server->close(); // 关闭 server 使连接必然失败

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy errSpy(&client, &WebSocketClient::error);
    EXPECT_TRUE(errSpy.wait(5000));
    EXPECT_FALSE(client.isConnected());
}

// T05 — 事件 payload 结构验证
TEST_F(WebSocketClientTest, T05_EventPayloadStructure) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        QTimer::singleShot(100, [sock]() {
            QJsonObject inner;
            inner["event"] = "scanner.ready";
            inner["data"] = QJsonObject{{"vesselId", 42}};
            QJsonObject pub;
            pub["type"] = "pub";
            pub["message"] = QString::fromUtf8(QJsonDocument(inner).toJson(QJsonDocument::Compact));
            sock->sendTextMessage(QJsonDocument(pub).toJson(QJsonDocument::Compact));
        });
    });

    WebSocketClient client;
    QSignalSpy evSpy(&client, &WebSocketClient::eventReceived);
    client.connectToServer(m_url);

    ASSERT_TRUE(evSpy.wait(3000));
    ASSERT_EQ(evSpy.count(), 1);

    QString evName = evSpy.at(0).at(0).toString();
    QJsonObject evObj = evSpy.at(0).at(1).toJsonObject();

    EXPECT_EQ(evName, "scanner.ready");
    EXPECT_TRUE(evObj.contains("data"));
    EXPECT_EQ(evObj["data"].toObject()["vesselId"].toInt(), 42);
}

// T06 — 地址切换重连
TEST_F(WebSocketClientTest, T06_AddressSwitchReconnects) {
    QObject::connect(m_server, &QWebSocketServer::newConnection,
                     [this]() { m_server->nextPendingConnection(); });

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));
    EXPECT_TRUE(client.isConnected());

    // 启动第二个 server
    QWebSocketServer server2("MockServer2", QWebSocketServer::NonSecureMode);
    ASSERT_TRUE(server2.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server2, &QWebSocketServer::newConnection,
                     [&server2]() { server2.nextPendingConnection(); });
    QString url2 = QString("ws://127.0.0.1:%1").arg(server2.serverPort());

    // 切换地址
    QSignalSpy connSpy2(&client, &WebSocketClient::connected);
    client.connectToServer(url2);

    EXPECT_TRUE(connSpy2.wait(3000)) << "Should reconnect to new address";
    EXPECT_TRUE(client.isConnected());

    server2.close();
}

// T07 — 重连后自动恢复订阅
TEST_F(WebSocketClientTest, T07_SubscriptionRestoredAfterReconnect) {
    QList<QString> receivedSubs;
    QWebSocket* serverSocket = nullptr;

    QObject::connect(m_server, &QWebSocketServer::newConnection, [&]() {
        serverSocket = m_server->nextPendingConnection();
        QObject::connect(serverSocket, &QWebSocket::textMessageReceived, [&](const QString& msg) {
            auto doc = QJsonDocument::fromJson(msg.toUtf8());
            if (doc.object()["type"].toString() == "sub") {
                receivedSubs.append(doc.object()["topic"].toString());
            }
        });
    });

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));

    // 订阅
    client.subscribe("vessel.notify");
    QCoreApplication::processEvents();
    ASSERT_EQ(client.subscriptions().size(), 1);

    receivedSubs.clear();

    // 启动新 server 并切换地址
    QSignalSpy connSpy2(&client, &WebSocketClient::connected);
    QWebSocketServer server2("MockServer2", QWebSocketServer::NonSecureMode);
    ASSERT_TRUE(server2.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server2, &QWebSocketServer::newConnection, [&]() {
        serverSocket = server2.nextPendingConnection();
        QObject::connect(serverSocket, &QWebSocket::textMessageReceived, [&](const QString& msg) {
            auto doc = QJsonDocument::fromJson(msg.toUtf8());
            if (doc.object()["type"].toString() == "sub") {
                receivedSubs.append(doc.object()["topic"].toString());
            }
        });
    });

    QString url2 = QString("ws://127.0.0.1:%1").arg(server2.serverPort());
    client.connectToServer(url2);
    ASSERT_TRUE(connSpy2.wait(3000));

    // 等待订阅恢复消息
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);

    EXPECT_TRUE(receivedSubs.contains("vessel.notify"))
        << "Subscription should be restored after reconnect";

    server2.close();
}

// T08 — disconnect 清空订阅
TEST_F(WebSocketClientTest, T08_DisconnectClearsSubscriptions) {
    QObject::connect(m_server, &QWebSocketServer::newConnection,
                     [this]() { m_server->nextPendingConnection(); });

    WebSocketClient client;
    client.connectToServer(m_url);
    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));

    client.subscribe("vessel.notify");
    EXPECT_EQ(client.subscriptions().size(), 1);

    client.disconnect();
    EXPECT_EQ(client.subscriptions().size(), 0);
    EXPECT_FALSE(client.isConnected());
}

// T09 — 空事件名被丢弃
TEST_F(WebSocketClientTest, T09_EmptyEventNameDiscarded) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        QTimer::singleShot(100, [sock]() {
            // 发送空事件名
            QJsonObject inner;
            inner["event"] = "";
            inner["data"] = QJsonObject{{"x", 1}};
            QJsonObject pub;
            pub["type"] = "pub";
            pub["message"] = QString::fromUtf8(QJsonDocument(inner).toJson(QJsonDocument::Compact));
            sock->sendTextMessage(QJsonDocument(pub).toJson(QJsonDocument::Compact));

            // 随后发送正常事件
            QTimer::singleShot(100, [sock]() {
                QJsonObject inner2;
                inner2["event"] = "scanner.ready";
                inner2["data"] = QJsonObject{{"ok", true}};
                QJsonObject pub2;
                pub2["type"] = "pub";
                pub2["message"] =
                    QString::fromUtf8(QJsonDocument(inner2).toJson(QJsonDocument::Compact));
                sock->sendTextMessage(QJsonDocument(pub2).toJson(QJsonDocument::Compact));
            });
        });
    });

    WebSocketClient client;
    QSignalSpy evSpy(&client, &WebSocketClient::eventReceived);
    client.connectToServer(m_url);

    // 等待正常事件到达（空名事件应被丢弃）
    ASSERT_TRUE(evSpy.wait(3000));

    // 应只收到 scanner.ready，空名事件被丢弃
    for (int i = 0; i < evSpy.count(); ++i) {
        EXPECT_FALSE(evSpy.at(i).at(0).toString().isEmpty())
            << "Empty event name should have been discarded";
    }
}

// T10 — disconnect 幂等（未连接时调用不崩溃）
TEST_F(WebSocketClientTest, T10_DisconnectIdempotent) {
    WebSocketClient client;
    EXPECT_FALSE(client.isConnected());

    client.disconnect();
    EXPECT_FALSE(client.isConnected());

    client.disconnect();
    EXPECT_FALSE(client.isConnected());
}

// T11 — 同地址重复 connectToServer 不触发新连接
TEST_F(WebSocketClientTest, T11_SameUrlNoReconnect) {
    QObject::connect(m_server, &QWebSocketServer::newConnection,
                     [this]() { m_server->nextPendingConnection(); });

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));
    EXPECT_TRUE(client.isConnected());
    EXPECT_EQ(client.currentUrl(), m_url);

    // 再次连接同地址，应直接返回 true 且不触发 connected 信号
    QSignalSpy connSpy2(&client, &WebSocketClient::connected);
    bool result = client.connectToServer(m_url);
    EXPECT_TRUE(result);
    EXPECT_TRUE(client.isConnected());
    // 短暂等待确认不触发 connected
    EXPECT_FALSE(connSpy2.wait(500)) << "Should NOT emit connected again for same URL";
}

// ============================================================
// 黑盒层：通过 QProcess 启动 driver_3dvision 子进程
// 经 stdin/stdout JSONL 协议验证 handler 真路径
// ============================================================

#include <QProcess>

// 查找 driver 可执行文件（CMake OUTPUT_NAME 为 stdio.drv.3dvision）
// 仅支持两种明确布局：
// 1) runtime/bin 运行：../data_root/drivers/stdio.drv.3dvision/stdio.drv.3dvision(.exe)
// 2) raw build 运行：./stdio.drv.3dvision(.exe)（与 stdiolink_tests 同目录）
static QString findDriverExe() {
    const QString drvName = QStringLiteral("stdio.drv.3dvision");
#ifdef Q_OS_WIN
    const QString exeName = drvName + ".exe";
#else
    const QString exeName = drvName;
#endif

    // 优先：runtime 布局（从 bin/ 同级的 data_root/drivers/ 查找）
    QString binDir = QCoreApplication::applicationDirPath();
    QDir runtimeDir(binDir + "/..");
    QString runtimePath =
        runtimeDir.absoluteFilePath("data_root/drivers/" + drvName + "/" + exeName);
    if (QFile::exists(runtimePath))
        return runtimePath;

    // raw build 布局：driver 与 stdiolink_tests 位于同目录
    const QString rawPath = QDir(binDir).absoluteFilePath(exeName);
    if (QFile::exists(rawPath))
        return rawPath;

    return {};
}

class Vision3DHandlerBlackBoxTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_driverExe = findDriverExe();
        ASSERT_FALSE(m_driverExe.isEmpty())
            << "Precondition failed: stdio.drv.3dvision executable not found. "
               "Expected runtime layout or raw build layout.";

        // 启动 mock WebSocket server
        m_server = new QWebSocketServer("MockServer", QWebSocketServer::NonSecureMode);
        ASSERT_TRUE(m_server->listen(QHostAddress::LocalHost, 0));
        m_wsPort = m_server->serverPort();

        QObject::connect(m_server, &QWebSocketServer::newConnection,
                         [this]() { m_serverSocket = m_server->nextPendingConnection(); });
    }

    void TearDown() override {
        if (m_server) {
            m_server->close();
            delete m_server;
        }
    }

    // 启动 driver 子进程（stdio 模式）
    QProcess* startDriver(const QStringList& extraArgs = {}) {
        auto* proc = new QProcess();
        proc->setProgram(m_driverExe);
        if (!extraArgs.isEmpty())
            proc->setArguments(extraArgs);
        proc->start();
        proc->waitForStarted(5000);
        return proc;
    }

    // 通过 stdin 发送 JSONL 命令
    void writeCommand(QProcess* proc, const QJsonObject& cmd) {
        QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
        proc->write(line);
        proc->waitForBytesWritten(1000);
    }

    // 从 stdout 读取一行 JSONL 响应（带超时）
    QJsonObject readResponse(QProcess* proc, int timeoutMs = 10000) {
        QElapsedTimer timer;
        timer.start();
        QByteArray buffer;
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (proc->waitForReadyRead(100) || proc->bytesAvailable() > 0) {
                buffer += proc->readAll();
                int idx = buffer.indexOf('\n');
                if (idx >= 0) {
                    QByteArray line = buffer.left(idx).trimmed();
                    if (!line.isEmpty()) {
                        auto doc = QJsonDocument::fromJson(line);
                        if (doc.isObject())
                            return doc.object();
                    }
                    buffer = buffer.mid(idx + 1);
                }
            }
        }
        // 兜底读取：进程快速退出时可能有尾部输出未被 waitForReadyRead 捕获
        buffer += proc->readAll();
        int idx = buffer.indexOf('\n');
        if (idx >= 0) {
            QByteArray line = buffer.left(idx).trimmed();
            if (!line.isEmpty()) {
                auto doc = QJsonDocument::fromJson(line);
                if (doc.isObject()) {
                    return doc.object();
                }
            }
        }
        return {};
    }

    // 从 stdout 读取多行 JSONL 响应直到超时
    QList<QJsonObject> readAllResponses(QProcess* proc, int timeoutMs = 10000) {
        QElapsedTimer timer;
        timer.start();
        QByteArray buffer;
        QList<QJsonObject> results;
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (proc->waitForReadyRead(100) || proc->bytesAvailable() > 0) {
                buffer += proc->readAll();
                while (true) {
                    int idx = buffer.indexOf('\n');
                    if (idx < 0)
                        break;
                    QByteArray line = buffer.left(idx).trimmed();
                    buffer = buffer.mid(idx + 1);
                    if (!line.isEmpty()) {
                        auto doc = QJsonDocument::fromJson(line);
                        if (doc.isObject())
                            results.append(doc.object());
                    }
                }
            }
        }
        // 兜底读取尾部数据
        buffer += proc->readAll();
        while (true) {
            int idx = buffer.indexOf('\n');
            if (idx < 0)
                break;
            QByteArray line = buffer.left(idx).trimmed();
            buffer = buffer.mid(idx + 1);
            if (!line.isEmpty()) {
                auto doc = QJsonDocument::fromJson(line);
                if (doc.isObject())
                    results.append(doc.object());
            }
        }
        return results;
    }

    QJsonObject readResponseByStatus(QProcess* proc, const QString& expectedStatus,
                                     int timeoutMs = 10000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            auto resp = readResponse(proc, 300);
            if (!resp.isEmpty() && resp["status"].toString() == expectedStatus) {
                return resp;
            }
        }
        return {};
    }

    QJsonObject readEventByName(QProcess* proc, const QString& eventName, int timeoutMs = 10000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            auto resp = readResponse(proc, 300);
            if (resp.isEmpty() || resp["status"].toString() != "event") {
                continue;
            }
            const auto dataObj = resp["data"].toObject();
            const QString name = dataObj.value("event").toString(resp["event"].toString());
            if (name == eventName) {
                return resp;
            }
        }
        return {};
    }

    void killDriver(QProcess* proc) {
        proc->kill();
        proc->waitForFinished(3000);
        delete proc;
    }

    QString m_driverExe;
    QWebSocketServer* m_server = nullptr;
    QWebSocket* m_serverSocket = nullptr;
    quint16 m_wsPort = 0;
};

// H01 — 悬空指针消除 + 事件扁平化验证
TEST_F(Vision3DHandlerBlackBoxTest, H01_EventPayloadFlat) {
    // KeepAlive 模式：需要多次命令交互（connect → 接收事件）
    auto* proc = startDriver({"--profile=keepalive"});
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    // 连接
    QJsonObject cmd;
    cmd["cmd"] = "ws.connect";
    cmd["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(m_wsPort)}};
    writeCommand(proc, cmd);

    QJsonObject resp = readResponseByStatus(proc, "done");
    ASSERT_EQ(resp["status"].toString(), "done");

    // 等待 server 端接收 client socket
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    ASSERT_NE(m_serverSocket, nullptr);

    // mock server 推送一条事件
    QJsonObject inner;
    inner["event"] = "scanner.ready";
    inner["data"] = QJsonObject{{"vesselId", 42}};
    QJsonObject pub;
    pub["type"] = "pub";
    pub["message"] = QString::fromUtf8(QJsonDocument(inner).toJson(QJsonDocument::Compact));
    m_serverSocket->sendTextMessage(QJsonDocument(pub).toJson(QJsonDocument::Compact));

    // 读取事件行
    // JSONL 协议: {"status":"event","code":0,"data":{"event":"...","data":{...}}}
    QJsonObject evResp = readEventByName(proc, "scanner.ready", 5000);
    EXPECT_EQ(evResp["status"].toString(), "event");
    QJsonObject evPayload = evResp["data"].toObject();
    EXPECT_EQ(evPayload["event"].toString(), "scanner.ready");
    // 验证扁平结构：data.data 应直接是业务负载，不应再包含 event 字段
    QJsonObject evData = evPayload["data"].toObject();
    EXPECT_FALSE(evData.contains("event")) << "Event data should be flat, not double-wrapped";
    EXPECT_EQ(evData["vesselId"].toInt(), 42);

    killDriver(proc);
}

// H02 — ws.connect 握手成功后返回 done
TEST_F(Vision3DHandlerBlackBoxTest, H02_ConnectReturnsDoneAfterHandshake) {
    auto* proc = startDriver();
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    QJsonObject cmd;
    cmd["cmd"] = "ws.connect";
    cmd["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(m_wsPort)}};
    writeCommand(proc, cmd);

    QJsonObject resp = readResponseByStatus(proc, "done");
    EXPECT_EQ(resp["status"].toString(), "done");
    EXPECT_TRUE(resp["data"].toObject()["connected"].toBool());

    killDriver(proc);
}

// H03a — ws.connect handler 真超时（本地 TCP 接受连接但不完成 WS 握手）
TEST_F(Vision3DHandlerBlackBoxTest, H03a_ConnectTimeout) {
    auto* proc = startDriver();
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    QTcpServer rawServer;
    ASSERT_TRUE(rawServer.listen(QHostAddress::LocalHost, 0));
    const quint16 rawPort = rawServer.serverPort();
    QObject::connect(&rawServer, &QTcpServer::newConnection, [&rawServer]() {
        auto* sock = rawServer.nextPendingConnection();
        Q_UNUSED(sock) // 接受 TCP 但不回 101，触发 handler 超时
    });

    QJsonObject cmd;
    cmd["cmd"] = "ws.connect";
    cmd["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(rawPort)}};

    QElapsedTimer timer;
    timer.start();
    writeCommand(proc, cmd);

    QJsonObject resp = readResponseByStatus(proc, "error", 15000); // 等待超过 5s timeout

    qint64 elapsed = timer.elapsed();
    ASSERT_FALSE(resp.isEmpty()) << "Expected timeout error response";

    // 正常超时路径
    EXPECT_EQ(resp["status"].toString(), "error");
    EXPECT_TRUE(
        resp["data"].toObject()["message"].toString().contains("timeout", Qt::CaseInsensitive));
    EXPECT_GE(elapsed, 4000) << "Should wait at least 4s before timeout";

    killDriver(proc);
    rawServer.close();
}

// H03b — ws.connect 连接被拒时返回 error
TEST_F(Vision3DHandlerBlackBoxTest, H03b_ConnectRefusedReturnsError) {
    m_server->close(); // 关闭 server 使连接被拒

    auto* proc = startDriver();
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    QJsonObject cmd;
    cmd["cmd"] = "ws.connect";
    cmd["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(m_wsPort)}};
    writeCommand(proc, cmd);

    const auto responses = readAllResponses(proc, 6000);
    bool foundError = false;
    for (const auto& resp : responses) {
        if (resp["status"].toString() == "error") {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError) << "Expected an error response in ws.connect refused path";

    killDriver(proc);
}

// H04 — ws.disconnect 幂等
TEST_F(Vision3DHandlerBlackBoxTest, H04_DisconnectIdempotent) {
    auto* proc = startDriver();
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    QJsonObject cmd;
    cmd["cmd"] = "ws.disconnect";
    cmd["data"] = QJsonObject{};
    writeCommand(proc, cmd);

    QJsonObject resp = readResponseByStatus(proc, "done");
    EXPECT_EQ(resp["status"].toString(), "done");
    EXPECT_TRUE(resp["data"].toObject()["disconnected"].toBool());

    killDriver(proc);
}

// H05 — ws.connect 重入防护
TEST_F(Vision3DHandlerBlackBoxTest, H05_ConnectReentryProtection) {
    // 使用 raw TCP server：接受 TCP 但不回 WebSocket 101 → 客户端停留在握手中
    m_server->close();

    QTcpServer rawServer;
    ASSERT_TRUE(rawServer.listen(QHostAddress::LocalHost, 0));
    quint16 rawPort = rawServer.serverPort();
    QObject::connect(&rawServer, &QTcpServer::newConnection, [&rawServer]() {
        auto* sock = rawServer.nextPendingConnection();
        Q_UNUSED(sock) // 拿住连接但不回 101
    });

    // KeepAlive 模式：必须支持多命令才能测试重入防护
    auto* proc = startDriver({"--profile=keepalive"});
    ASSERT_NE(proc->state(), QProcess::NotRunning);

    // 一次写入两条 ws.connect，确保 reader 线程连续投递命令。
    // 第一条将进入 QEventLoop，第二条应被 m_wsConnecting 拦截。
    QJsonObject cmd1;
    cmd1["cmd"] = "ws.connect";
    cmd1["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(rawPort)}};
    QJsonObject cmd2;
    cmd2["cmd"] = "ws.connect";
    cmd2["data"] = QJsonObject{{"addr", QString("127.0.0.1:%1").arg(rawPort)}};
    QByteArray twoLines = QJsonDocument(cmd1).toJson(QJsonDocument::Compact) + "\n" +
                          QJsonDocument(cmd2).toJson(QJsonDocument::Compact) + "\n";
    proc->write(twoLines);
    proc->waitForBytesWritten(1000);

    // 读取所有响应（最多等 8s，覆盖 5s 超时 + 余量）
    auto responses = readAllResponses(proc, 8000);

    bool foundReentryError = false;
    for (const auto& r : responses) {
        if (r["status"].toString() == "error") {
            QString msg = r["data"].toObject()["message"].toString();
            if (msg.contains("already in progress", Qt::CaseInsensitive)) {
                foundReentryError = true;
            }
        }
    }

    EXPECT_TRUE(foundReentryError)
        << "Expected reentry guard error: ws.connect already in progress";

    killDriver(proc);
    rawServer.close();
}

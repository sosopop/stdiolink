#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <memory>

#include "stdiolink/guard/process_guard_client.h"
#include "stdiolink/guard/process_guard_server.h"

using namespace stdiolink;

namespace {

QString stubPath() {
    return QCoreApplication::applicationDirPath() + "/test_guard_stub";
}

} // namespace

// T01 — Server listen 成功
TEST(ProcessGuard, T01_ServerListenSuccess) {
    ProcessGuardServer server;
    EXPECT_TRUE(server.start());
    EXPECT_TRUE(server.isListening());
    EXPECT_FALSE(server.guardName().isEmpty());
    EXPECT_TRUE(server.guardName().startsWith("stdiolink_guard_"));
}

// T02 — Server listen 失败（名称冲突）
TEST(ProcessGuard, T02_ServerListenConflict) {
#ifdef Q_OS_WIN
    GTEST_SKIP() << "Windows named pipes allow multiple listeners on the same name";
#endif
    ProcessGuardServer server1;
    EXPECT_TRUE(server1.start("fixed_test_guard_name"));

    ProcessGuardServer server2;
    EXPECT_FALSE(server2.start("fixed_test_guard_name"));
    EXPECT_FALSE(server2.isListening());
}

// T03 — Server stop 触发客户端断开 (进程级验证)
TEST(ProcessGuard, T03_ServerStopTriggersClientExit) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    QProcess stub;
    stub.start(stubPath(), {"--guard=" + server.guardName()});
    ASSERT_TRUE(stub.waitForStarted(5000));

    // Give client time to connect
    QThread::msleep(300);

    // Stop server — client should detect disconnect and forceFastExit(1)
    server.stop();

    ASSERT_TRUE(stub.waitForFinished(5000));
    EXPECT_EQ(stub.exitCode(), 1);
}

// T04 — Server 析构自动 stop
TEST(ProcessGuard, T04_ServerDestructorAutoStop) {
    QString name;
    {
        ProcessGuardServer server;
        ASSERT_TRUE(server.start());
        name = server.guardName();
        EXPECT_TRUE(server.isListening());
    }
    // After destruction, should not be connectable
    QLocalSocket sock;
    sock.connectToServer(name);
    EXPECT_FALSE(sock.waitForConnected(500));
}

// T05 — guardName 格式
TEST(ProcessGuard, T05_GuardNameFormat) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());
    const QString name = server.guardName();
    QRegularExpression re("^stdiolink_guard_[0-9a-f-]+$");
    EXPECT_TRUE(re.match(name).hasMatch()) << name.toStdString();
}

// T06 — Client 连接成功保持存活 (进程级验证)
TEST(ProcessGuard, T06_ClientConnectedStaysAlive) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    QProcess stub;
    stub.start(stubPath(), {"--guard=" + server.guardName()});
    ASSERT_TRUE(stub.waitForStarted(5000));

    // Wait and verify process is still running
    QThread::msleep(500);
    EXPECT_EQ(stub.state(), QProcess::Running);

    // Cleanup
    server.stop();
    stub.waitForFinished(5000);
}

// T07 — Client 连接失败（server 不存在）
TEST(ProcessGuard, T07_ClientConnectFailNoServer) {
    QProcess stub;
    stub.start(stubPath(), {"--guard=stdiolink_guard_nonexistent_12345"});
    ASSERT_TRUE(stub.waitForStarted(5000));

    ASSERT_TRUE(stub.waitForFinished(10000));
    EXPECT_EQ(stub.exitCode(), 1);
}

// T08 — Client 检测 server 关闭
TEST(ProcessGuard, T08_ClientDetectsServerClose) {
    auto server = std::make_unique<ProcessGuardServer>();
    ASSERT_TRUE(server->start());

    QProcess stub;
    stub.start(stubPath(), {"--guard=" + server->guardName()});
    ASSERT_TRUE(stub.waitForStarted(5000));

    // Give client time to connect
    QThread::msleep(300);

    // Destroy server
    server.reset();

    ASSERT_TRUE(stub.waitForFinished(5000));
    EXPECT_EQ(stub.exitCode(), 1);
}

// T09 — Client stop 正常退出
TEST(ProcessGuard, T09_ClientStopNormal) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    ProcessGuardClient client(server.guardName());
    client.start();

    QThread::msleep(300);

    // stop() should not trigger forceFastExit
    client.stop();

    // If we reach here, forceFastExit was not called
    SUCCEED();
}

// T10 — Client 析构自动 stop
TEST(ProcessGuard, T10_ClientDestructorAutoStop) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    {
        ProcessGuardClient client(server.guardName());
        client.start();
        QThread::msleep(300);
    }

    // If we reach here, destructor called stop() without forceFastExit
    SUCCEED();
}

// T11 — startFromArgs 有 --guard
TEST(ProcessGuard, T11_StartFromArgsWithGuard) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    auto client = ProcessGuardClient::startFromArgs(
        {"app", "--guard=" + server.guardName()});
    EXPECT_NE(client, nullptr);

    if (client) {
        client->stop();
    }
}

// T12 — startFromArgs 无 --guard
TEST(ProcessGuard, T12_StartFromArgsNoGuard) {
    auto client = ProcessGuardClient::startFromArgs(
        {"app", "--config.key=val"});
    EXPECT_EQ(client, nullptr);
}

// T13 — startFromArgs --guard 值为空
TEST(ProcessGuard, T13_StartFromArgsEmptyGuard) {
    auto client = ProcessGuardClient::startFromArgs(
        {"app", "--guard="});
    EXPECT_EQ(client, nullptr);
}

// T14 — 多客户端连接同一 server
TEST(ProcessGuard, T14_MultipleClientsOneServer) {
    ProcessGuardServer server;
    ASSERT_TRUE(server.start());

    QProcess stub1;
    stub1.start(stubPath(), {"--guard=" + server.guardName()});
    ASSERT_TRUE(stub1.waitForStarted(5000));

    QProcess stub2;
    stub2.start(stubPath(), {"--guard=" + server.guardName()});
    ASSERT_TRUE(stub2.waitForStarted(5000));

    // Give clients time to connect
    QThread::msleep(300);

    // Both should be running
    EXPECT_EQ(stub1.state(), QProcess::Running);
    EXPECT_EQ(stub2.state(), QProcess::Running);

    // Stop server — both should exit with code 1
    server.stop();

    ASSERT_TRUE(stub1.waitForFinished(5000));
    EXPECT_EQ(stub1.exitCode(), 1);

    ASSERT_TRUE(stub2.waitForFinished(5000));
    EXPECT_EQ(stub2.exitCode(), 1);
}

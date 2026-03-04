#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <memory>
#include <gtest/gtest.h>
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink;

static const QString TestDriver =
    PlatformUtils::executablePath(".", "test_driver");

namespace {

void cleanupProcess(QProcess* proc) {
    if (proc == nullptr) {
        return;
    }
    if (proc->state() != QProcess::NotRunning) {
        proc->kill();
        proc->waitForFinished(1000);
    }
    delete proc;
}

using ProcessPtr = std::unique_ptr<QProcess, decltype(&cleanupProcess)>;

} // namespace

class DriverCoreAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_driverPath = QCoreApplication::applicationDirPath() + "/" + TestDriver;
    }

    QProcess* startDriverWithArgs(const QStringList& args) {
        auto* proc = new QProcess();
        proc->setProgram(m_driverPath);
        proc->setArguments(args);
        proc->start();
        if (!proc->waitForStarted(5000)) {
            ADD_FAILURE() << "Failed to start test_driver";
            cleanupProcess(proc);
            return nullptr;
        }
        return proc;
    }

    QProcess* startDriver(const QString& profile) {
        return startDriverWithArgs({"--profile=" + profile});
    }

    void writeCommand(QProcess* proc, const QString& cmd,
                      const QJsonObject& data = {}) {
        QJsonObject req{{"cmd", cmd}};
        if (!data.isEmpty()) {
            req["data"] = data;
        }
        const QByteArray line =
            QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
        proc->write(line);
        proc->waitForBytesWritten(1000);
    }

    bool readResponse(QProcess* proc, QJsonObject& response, int timeoutMs = 5000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (proc->canReadLine()) {
                const QByteArray line = proc->readLine().trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(line);
                if (!doc.isNull() && doc.isObject()) {
                    response = doc.object();
                    return true;
                }
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }

    QString m_driverPath;
};

// R01: KeepAlive 下定时器回调可触发。
TEST_F(DriverCoreAsyncTest, R01_KeepAlive_TimerCallbackFires) {
    ProcessPtr proc(startDriver("keepalive"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    writeCommand(proc.get(), "timer_echo", {{"delay", 100}});

    QJsonObject resp;
    const bool got = readResponse(proc.get(), resp, 3000);
    EXPECT_TRUE(got) << "timer_echo should receive done response via QTimer callback";
    if (got) {
        EXPECT_EQ(resp["status"].toString(), "done");
        EXPECT_TRUE(resp["data"].toObject()["timer_fired"].toBool());
    }

    proc->closeWriteChannel();
    ASSERT_TRUE(proc->waitForFinished(5000));
}

// R02: KeepAlive 下 EOF 触发退出。
TEST_F(DriverCoreAsyncTest, R02_KeepAlive_EOF_Exit) {
    ProcessPtr proc(startDriver("keepalive"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    writeCommand(proc.get(), "echo", {{"msg", "hello"}});
    QJsonObject resp;
    ASSERT_TRUE(readResponse(proc.get(), resp, 3000));

    proc->closeWriteChannel();

    QElapsedTimer timer;
    timer.start();
    ASSERT_TRUE(proc->waitForFinished(5000)) << "Driver should exit after stdin EOF";
    EXPECT_LT(timer.elapsed(), 4000) << "Driver should exit promptly after EOF, not hang";
    EXPECT_EQ(proc->exitCode(), 0);
}

// R03: OneShot 仅处理首条命令。
TEST_F(DriverCoreAsyncTest, R03_OneShot_OnlyFirstCommand) {
    ProcessPtr proc(startDriver("oneshot"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    QJsonObject req1{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "first"}}}};
    QJsonObject req2{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "second"}}}};
    const QByteArray batch =
        QJsonDocument(req1).toJson(QJsonDocument::Compact) + "\n"
        + QJsonDocument(req2).toJson(QJsonDocument::Compact) + "\n";
    proc->write(batch);
    proc->waitForBytesWritten(1000);

    ASSERT_TRUE(proc->waitForFinished(5000));

    const QByteArray allOutput = proc->readAllStandardOutput();
    const QList<QByteArray> lines = allOutput.split('\n');

    int doneCount = 0;
    for (const auto& line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (doc.isObject() && doc.object()["status"].toString() == "done") {
            doneCount++;
            const QJsonObject data = doc.object()["data"].toObject();
            EXPECT_EQ(data["msg"].toString(), "first");
        }
    }
    EXPECT_EQ(doneCount, 1) << "OneShot should only process the first command";
}

// R04: OneShot 退出不挂死。
TEST_F(DriverCoreAsyncTest, R04_OneShot_ExitNotHang) {
    ProcessPtr proc(startDriver("oneshot"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    QElapsedTimer timer;
    timer.start();

    writeCommand(proc.get(), "echo", {{"msg", "bye"}});

    ASSERT_TRUE(proc->waitForFinished(5000))
        << "OneShot driver should exit after processing one command";
    EXPECT_LT(timer.elapsed(), 4000)
        << "OneShot exit should not hang waiting for stdin reader";
    EXPECT_EQ(proc->exitCode(), 0);
}

// R05: 空行应被忽略。
TEST_F(DriverCoreAsyncTest, R05_EmptyLineIgnored) {
    ProcessPtr proc(startDriver("keepalive"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    proc->write("\n");
    proc->waitForBytesWritten(1000);
    writeCommand(proc.get(), "echo", {{"msg", "after_empty"}});

    QJsonObject resp;
    ASSERT_TRUE(readResponse(proc.get(), resp, 3000));
    EXPECT_EQ(resp["status"].toString(), "done");
    EXPECT_EQ(resp["data"].toObject()["msg"].toString(), "after_empty");

    proc->closeWriteChannel();
    ASSERT_TRUE(proc->waitForFinished(5000));
}

// R06: 非法 JSON 错误码保持 1000，且后续请求仍可处理。
TEST_F(DriverCoreAsyncTest, R06_InvalidJsonErrorCode) {
    ProcessPtr proc(startDriver("keepalive"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    proc->write("{bad json}\n");
    proc->waitForBytesWritten(1000);

    QJsonObject resp1;
    ASSERT_TRUE(readResponse(proc.get(), resp1, 3000));
    EXPECT_EQ(resp1["status"].toString(), "error");
    EXPECT_EQ(resp1["code"].toInt(), 1000);

    writeCommand(proc.get(), "echo", {{"msg", "recovered"}});
    QJsonObject resp2;
    ASSERT_TRUE(readResponse(proc.get(), resp2, 3000));
    EXPECT_EQ(resp2["status"].toString(), "done");

    proc->closeWriteChannel();
    ASSERT_TRUE(proc->waitForFinished(5000));
}

// R07: 无后续 stdin 输入时异步 event 仍应触发。
TEST_F(DriverCoreAsyncTest, R07_AsyncEventWithoutFurtherStdin) {
    ProcessPtr proc(startDriver("keepalive"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    writeCommand(proc.get(), "async_event_once");

    QJsonObject respDone;
    ASSERT_TRUE(readResponse(proc.get(), respDone, 3000));
    EXPECT_EQ(respDone["status"].toString(), "done");

    QJsonObject respEvent;
    const bool got = readResponse(proc.get(), respEvent, 3000);
    EXPECT_TRUE(got) << "Should receive async event without further stdin input";
    if (got) {
        EXPECT_EQ(respEvent["status"].toString(), "event");
        EXPECT_EQ(respEvent["data"].toObject()["event"].toString(), "tick");
    }

    proc->closeWriteChannel();
    ASSERT_TRUE(proc->waitForFinished(5000));
}

// R08: OneShot 下尾随命令应被忽略。
TEST_F(DriverCoreAsyncTest, R08_OneShot_TrailingCommandIgnored) {
    ProcessPtr proc(startDriver("oneshot"), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    QJsonObject req1{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "first"}}}};
    QJsonObject req2{{"cmd", "progress"}, {"data", QJsonObject{{"steps", 3}}}};
    const QByteArray batch =
        QJsonDocument(req1).toJson(QJsonDocument::Compact) + "\n"
        + QJsonDocument(req2).toJson(QJsonDocument::Compact) + "\n";
    proc->write(batch);
    proc->waitForBytesWritten(1000);

    ASSERT_TRUE(proc->waitForFinished(5000));

    const QByteArray allOutput = proc->readAllStandardOutput();
    EXPECT_FALSE(allOutput.contains("\"step\""))
        << "OneShot should not process trailing progress command";
}

// R09: Console 模式下 done 仍应触发退出。
TEST_F(DriverCoreAsyncTest, R09_Console_DoneTerminatesProcess) {
    ProcessPtr proc(startDriverWithArgs(
        {"--mode=console", "--cmd=echo", "--msg=console_done"}), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    ASSERT_TRUE(proc->waitForFinished(5000));
    EXPECT_EQ(proc->exitCode(), 0);

    const QByteArray stdoutText = proc->readAllStandardOutput();
    EXPECT_TRUE(stdoutText.contains("\"status\":\"done\""));
    EXPECT_TRUE(stdoutText.contains("\"console_done\""));
}

// R10: Console 模式下仅 event/无终止响应时应持续运行，直到外部终止。
TEST_F(DriverCoreAsyncTest, R10_Console_NoTerminalResponseStaysAlive) {
    ProcessPtr proc(startDriverWithArgs(
        {"--mode=console", "--cmd=noop"}), cleanupProcess);
    ASSERT_NE(proc, nullptr);

    EXPECT_FALSE(proc->waitForFinished(1200))
        << "Console mode should keep event loop alive without done/error";
    EXPECT_EQ(proc->state(), QProcess::Running);
}

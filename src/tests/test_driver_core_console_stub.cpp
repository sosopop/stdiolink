#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

#include "stdiolink/platform/platform_utils.h"

namespace {

QString consoleStubPath() {
    return stdiolink::PlatformUtils::executablePath(QCoreApplication::applicationDirPath(),
                                                    "test_console_meta_driver");
}

QProcessEnvironment processEnvWithBinPath() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString binDir = QCoreApplication::applicationDirPath();
    const QString oldPath = env.value("PATH");
    env.insert("PATH", oldPath.isEmpty() ? binDir : binDir + QDir::listSeparator() + oldPath);
    return env;
}

QJsonObject parseSingleLineJson(const QByteArray& stdoutData) {
    const QList<QByteArray> lines = stdoutData.trimmed().split('\n');
    if (lines.isEmpty()) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(lines.last()).object();
}

struct ProcessResult {
    int exitCode = -1;
    QJsonObject response;
};

ProcessResult runConsoleStub(const QStringList& arguments, const QByteArray& stdinData = {}) {
    const QString exe = consoleStubPath();
    EXPECT_TRUE(QFileInfo::exists(exe)) << exe.toStdString();

    QProcess proc;
    proc.setProcessEnvironment(processEnvWithBinPath());
    proc.start(exe, arguments);
    EXPECT_TRUE(proc.waitForStarted(5000));
    if (!stdinData.isEmpty()) {
        proc.write(stdinData);
    }
    proc.closeWriteChannel();
    EXPECT_TRUE(proc.waitForFinished(10000));
    return ProcessResult{proc.exitCode(), parseSingleLineJson(proc.readAllStandardOutput())};
}

} // namespace

TEST(DriverCoreConsoleStub, T09_StringAndEnumMetaKeepNumericLikeStrings) {
    const QJsonObject resp = runConsoleStub(
                                 {"--cmd=run",
                                  "--password=123456",
                                  "--mode_code=1",
                                  "--units[0].id=1",
                                  "--units[0].size=10000"})
                                 .response;
    EXPECT_EQ(resp.value("status").toString(), "done");
    const QJsonObject data = resp.value("data").toObject();
    EXPECT_TRUE(data.value("password").isString());
    EXPECT_EQ(data.value("password").toString(), "123456");
    EXPECT_TRUE(data.value("mode_code").isString());
    EXPECT_EQ(data.value("mode_code").toString(), "1");
    const QJsonArray units = data.value("units").toArray();
    ASSERT_EQ(units.size(), 1);
    EXPECT_EQ(units[0].toObject().value("id").toInt(), 1);
    EXPECT_EQ(units[0].toObject().value("size").toInt(), 10000);
}

TEST(DriverCoreConsoleStub, T10_ArrayObjectPathBuildsUnitsAndHitsLeafMeta) {
    const QJsonObject resp = runConsoleStub(
                                 {"--cmd=run",
                                  "--password=123456",
                                  "--mode_code=1",
                                  "--units[0].id=1",
                                  "--units[0].size=10000",
                                  "--units[1].id=2",
                                  "--units[1].size=20000"})
                                 .response;
    EXPECT_EQ(resp.value("status").toString(), "done");
    const QJsonArray units = resp.value("data").toObject().value("units").toArray();
    ASSERT_EQ(units.size(), 2);
    EXPECT_EQ(units[0].toObject().value("id").toInt(), 1);
    EXPECT_EQ(units[1].toObject().value("size").toInt(), 20000);
}

TEST(DriverCoreConsoleStub, T11_UnknownFieldFallsBackToFriendlyInference) {
    const QJsonObject resp = runConsoleStub(
                                 {"--cmd=run",
                                  "--password=123456",
                                  "--mode_code=1",
                                  "--threshold=3.5",
                                  "--enabled=true"})
                                 .response;
    EXPECT_EQ(resp.value("status").toString(), "done");
    const QJsonObject data = resp.value("data").toObject();
    EXPECT_TRUE(data.value("threshold").isDouble());
    EXPECT_DOUBLE_EQ(data.value("threshold").toDouble(), 3.5);
    EXPECT_TRUE(data.value("enabled").toBool());
}

TEST(DriverCoreConsoleStub, T12_LegacyDotPathStillBuildsNestedObject) {
    const QJsonObject resp =
        runConsoleStub({"--cmd=run", "--password=123456", "--mode_code=1", "--roi.x=10", "--roi.y=20"})
            .response;
    EXPECT_EQ(resp.value("status").toString(), "done");
    const QJsonObject roi = resp.value("data").toObject().value("roi").toObject();
    EXPECT_EQ(roi.value("x").toInt(), 10);
    EXPECT_EQ(roi.value("y").toInt(), 20);
}

TEST(DriverCoreConsoleStub, T13_StdioStrictTypeValidationRemainsUnchanged) {
    const QByteArray request =
        "{\"id\":\"1\",\"cmd\":\"run\",\"data\":{\"password\":123456,\"mode_code\":\"1\"}}\n";
    const ProcessResult result =
        runConsoleStub({"--mode=stdio", "--profile=oneshot"}, request);
    const QJsonObject resp = result.response;
    EXPECT_EQ(resp.value("status").toString(), "error");
    const QJsonObject data = resp.value("data").toObject();
    EXPECT_EQ(data.value("name").toString(), "ValidationFailed");
    EXPECT_TRUE(data.value("message").toString().contains("password"));
    EXPECT_TRUE(data.value("message").toString().contains("expected string"));
}

TEST(DriverCoreConsoleStub, T17_Int64OutOfSafeRangeFails) {
    const ProcessResult result =
        runConsoleStub({"--cmd=run",
                        "--password=123456",
                        "--mode_code=1",
                        "--safe_counter=9007199254740993"});
    EXPECT_NE(result.exitCode, 0);

    const QJsonObject resp = result.response;
    EXPECT_EQ(resp.value("status").toString(), "error");
    const QJsonObject data = resp.value("data").toObject();
    EXPECT_EQ(data.value("name").toString(), "CliParseFailed");
    EXPECT_TRUE(data.value("message").toString().contains("safe range"));
}

TEST(DriverCoreConsoleStub, T18_ContainerLiteralAndChildPathConflictReturnsCliParseFailed) {
    const ProcessResult result =
        runConsoleStub({"--cmd=run",
                        "--password=123456",
                        "--mode_code=1",
                        "--units=[{\"id\":1}]",
                        "--units[0].size=2"});
    EXPECT_NE(result.exitCode, 0);

    const QJsonObject resp = result.response;
    EXPECT_EQ(resp.value("status").toString(), "error");
    const QJsonObject data = resp.value("data").toObject();
    EXPECT_EQ(data.value("name").toString(), "CliParseFailed");
    EXPECT_TRUE(data.value("message").toString().contains("container literal vs child path"));
}

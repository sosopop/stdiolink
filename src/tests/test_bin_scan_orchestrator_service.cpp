#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "helpers/bin_scan_fake_vision_server.h"
#include "helpers/plc_crane_sim_handle.h"
#include "stdiolink/platform/platform_utils.h"

namespace {

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QString stdoutText;
    QString stderrText;
};

QString findRepoRootPath() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(dir.absoluteFilePath("src/data_root/services"))
            && QFileInfo::exists(dir.absoluteFilePath("doc/milestone"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString runtimeDataRootPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(appDir).absoluteFilePath("../data_root"),
        QDir(appDir).absoluteFilePath("data_root"),
        QDir(appDir).absoluteFilePath("../runtime_debug/data_root"),
        QDir(appDir).absoluteFilePath("../runtime_release/data_root"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate + "/drivers")) {
            return candidate;
        }
    }
    return {};
}

QString serviceExecutablePath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        stdiolink::PlatformUtils::executablePath(appDir, "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(QDir(appDir).absoluteFilePath("../runtime_release/bin"),
                                                 "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(QDir(appDir).absoluteFilePath("../runtime_debug/bin"),
                                                 "stdiolink_service"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.front();
}

QString serviceSourceDirPath() {
    return QDir(findRepoRootPath()).absoluteFilePath(
        "src/data_root/services/bin_scan_orchestrator");
}

QString runtimeDriverDirPath(const QString& driverName) {
    return QDir(runtimeDataRootPath()).absoluteFilePath("drivers/" + driverName);
}

bool copyFile(const QString& src, const QString& dst) {
    QFile::remove(dst);
    QFileInfo dstInfo(dst);
    QDir().mkpath(dstInfo.absolutePath());
    return QFile::copy(src, dst);
}

bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        QDir().mkpath(dstPath);
        QDir dir(srcPath);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
        for (const QFileInfo& entry : entries) {
            const QString dstChild = QDir(dstPath).filePath(entry.fileName());
            if (!copyRecursively(entry.absoluteFilePath(), dstChild)) {
                return false;
            }
        }
        return true;
    }
    return copyFile(srcPath, dstPath);
}

QProcessEnvironment childEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList extraDirs{
        appDir,
        QDir(appDir).absoluteFilePath("../runtime_release/bin"),
        QDir(appDir).absoluteFilePath("../runtime_debug/bin"),
    };
    QStringList existingDirs;
    for (const QString& dir : extraDirs) {
        if (QFileInfo::exists(dir)) {
            existingDirs.append(dir);
        }
    }
    const QString oldPath = env.value("PATH");
    const QString extraPath = existingDirs.join(QDir::listSeparator());
    env.insert("PATH", oldPath.isEmpty() ? extraPath : (extraPath + QDir::listSeparator() + oldPath));
    return env;
}

QString readUtf8File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    return predicate();
}

int countOccurrences(const QString& haystack, const QString& needle) {
    if (needle.isEmpty()) {
        return 0;
    }
    int count = 0;
    int pos = 0;
    while ((pos = haystack.indexOf(needle, pos)) >= 0) {
        ++count;
        pos += needle.size();
    }
    return count;
}

bool canListenOnPort(quint16 port) {
    QTcpServer server;
    return server.listen(QHostAddress::AnyIPv4, port);
}

#ifdef Q_OS_WIN
QString normalizeWinPath(const QString& path) {
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower();
}

QList<DWORD> findProcessesByExecutablePath(const QString& exePath) {
    QList<DWORD> pids;
    const QString normalizedExePath = normalizeWinPath(exePath);
    if (normalizedExePath.isEmpty()) {
        return pids;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                         FALSE,
                                         entry.th32ProcessID);
            if (!process) {
                continue;
            }

            wchar_t buffer[MAX_PATH];
            DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
            if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
                const QString imagePath = normalizeWinPath(QString::fromWCharArray(buffer, static_cast<int>(size)));
                if (imagePath == normalizedExePath) {
                    pids.append(entry.th32ProcessID);
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pids;
}

bool waitForNoProcessByExecutablePath(const QString& exePath, int timeoutMs) {
    return waitUntil([&]() { return findProcessesByExecutablePath(exePath).isEmpty(); }, timeoutMs);
}

void killProcessesByExecutablePath(const QString& exePath) {
    const QList<DWORD> pids = findProcessesByExecutablePath(exePath);
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!process) {
            continue;
        }
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
}
#else
bool waitForNoProcessByExecutablePath(const QString&, int) {
    return true;
}

void killProcessesByExecutablePath(const QString&) {
}
#endif

} // namespace

class BinScanOrchestratorServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!canListenOnPort(6200)) {
            GTEST_SKIP() << "TCP port 6200 is already in use; a running stdiolink_server or other"
                            " local process may interfere with BinScanOrchestratorServiceTest";
        }

        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(QFileInfo::exists(serviceExecutablePath()));
        ASSERT_TRUE(QFileInfo::exists(serviceSourceDirPath()));
        ASSERT_TRUE(QFileInfo::exists(PlcCraneSimHandle::findExecutable()));
        ASSERT_TRUE(QFileInfo::exists(runtimeDriverDirPath("stdio.drv.plc_crane")));
        ASSERT_TRUE(QFileInfo::exists(runtimeDriverDirPath("stdio.drv.3dvision")));

        m_dataRoot = m_tmpDir.path() + "/data_root";
        ASSERT_TRUE(QDir().mkpath(m_dataRoot + "/services"));
        ASSERT_TRUE(QDir().mkpath(m_dataRoot + "/drivers"));
        ASSERT_TRUE(QDir().mkpath(m_dataRoot + "/projects"));
        ASSERT_TRUE(QDir().mkpath(m_dataRoot + "/logs"));
        ASSERT_TRUE(QDir().mkpath(m_dataRoot + "/workspaces"));

        ASSERT_TRUE(copyRecursively(serviceSourceDirPath(),
                                    m_dataRoot + "/services/bin_scan_orchestrator"));
        ASSERT_TRUE(copyRecursively(runtimeDriverDirPath("stdio.drv.plc_crane"),
                                    m_dataRoot + "/drivers/stdio.drv.plc_crane"));
        ASSERT_TRUE(copyRecursively(runtimeDriverDirPath("stdio.drv.3dvision"),
                                    m_dataRoot + "/drivers/stdio.drv.3dvision"));
    }

    QString serviceDirPath() const {
        return m_dataRoot + "/services/bin_scan_orchestrator";
    }

    QString configPath(const QString& name = "config.json") const {
        return m_tmpDir.path() + "/" + name;
    }

    QString resultPath(const QString& name = "result.json") const {
        return m_tmpDir.path() + "/" + name;
    }

    QString visionDriverExecutablePath() const {
        return stdiolink::PlatformUtils::executablePath(
            m_dataRoot + "/drivers/stdio.drv.3dvision",
            "stdio.drv.3dvision");
    }

    QString writeConfig(const QJsonObject& obj, const QString& name = "config.json") const {
        const QString path = configPath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
        return path;
    }

    QJsonObject baseConfig(const QString& visionAddr, const QJsonArray& cranes) const {
        return QJsonObject{
            {"vessel_id", 15},
            {"vision", QJsonObject{
                           {"addr", visionAddr},
                           {"user_name", "admin"},
                           {"password", "123456"},
                           {"view_mode", false},
                       }},
            {"cranes", cranes},
            {"crane_poll_interval_ms", 100},
            {"crane_wait_timeout_ms", 2000},
            {"scan_request_timeout_ms", 500},
            {"scan_start_retry_count", 2},
            {"scan_start_retry_interval_ms", 50},
            {"scan_poll_interval_ms", 200},
            {"scan_poll_fail_limit", 5},
            {"scan_timeout_ms", 2000},
            {"clock_skew_tolerance_ms", 2000},
            {"on_error_set_manual", true},
            {"result_output_path", resultPath()},
        };
    }

    QJsonObject craneConfig(const PlcCraneSimHandle& crane, const QString& id) const {
        return QJsonObject{
            {"id", id},
            {"host", crane.host()},
            {"port", static_cast<int>(crane.port())},
            {"unit_id", static_cast<int>(crane.unitId())},
            {"timeout_ms", 1000},
        };
    }

    RunResult runService(const QString& cfgPath, int timeoutMs = 40000) const {
        QProcess proc;
        proc.setProcessEnvironment(childEnv());
        proc.start(serviceExecutablePath(), {
            serviceDirPath(),
            "--data-root=" + m_dataRoot,
            "--config-file=" + cfgPath,
        });

        RunResult result;
        if (!proc.waitForStarted(3000)) {
            result.stderrText = "failed to start stdiolink_service";
            return result;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (proc.waitForFinished(20)) {
                result.finished = true;
                break;
            }
        }
        if (!result.finished) {
            proc.kill();
            proc.waitForFinished(3000);
        }

        result.exitCode = proc.exitCode();
        result.stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
        result.stderrText = QString::fromUtf8(proc.readAllStandardError());
        return result;
    }

    QTemporaryDir m_tmpDir;
    QString m_dataRoot;
};

TEST_F(BinScanOrchestratorServiceTest, T01_ValidConfigEntersMainFlow) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const QJsonArray cranes{craneConfig(crane, "crane_a")};
    const QString cfgPath = writeConfig(baseConfig(vision.baseUrl().mid(QString("http://").size()), cranes));

    const RunResult r = runService(cfgPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_FALSE(r.stderrText.contains("required field"));
    EXPECT_FALSE(r.stderrText.contains("config validation"));
}

TEST_F(BinScanOrchestratorServiceTest, T02_EmptyCraneListRejected) {
    const QJsonObject cfg = baseConfig("127.0.0.1:6100", QJsonArray{});
    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("cranes"));
}

TEST_F(BinScanOrchestratorServiceTest, T03_SingleCraneSuccess) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const QJsonArray cranes{craneConfig(crane, "crane_a")};
    const RunResult r = runService(
        writeConfig(baseConfig(vision.baseUrl().mid(QString("http://").size()), cranes)));

    EXPECT_TRUE(r.finished) << qPrintable(r.stderrText);
    EXPECT_EQ(r.exitCode, 0) << qPrintable(r.stderrText);
    EXPECT_TRUE(r.stderrText.contains("scan completed")) << qPrintable(r.stderrText);
    EXPECT_EQ(vision.lastLoginBody().value("userName").toString(), "admin");
    EXPECT_EQ(vision.lastLoginBody().value("password").toString(), "123456");
    EXPECT_EQ(vision.lastLoginBody().value("viewMode").toBool(), false);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(readUtf8File(resultPath()).toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError) << qPrintable(readUtf8File(resultPath()));
    ASSERT_TRUE(doc.isObject());
    const QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("vesselId").toInt(), 15);
    EXPECT_EQ(obj.value("status").toString(), "success");
}

TEST_F(BinScanOrchestratorServiceTest, T04_AutoModeFailureAbortsBeforeScan) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");

    PlcCraneSimHandle goodCrane = PlcCraneSimHandle::create();
    ASSERT_TRUE(goodCrane.start()) << qPrintable(goodCrane.error());

    QJsonArray cranes{
        craneConfig(goodCrane, "crane_ok"),
        QJsonObject{
            {"id", "crane_bad"},
            {"host", "127.0.0.1"},
            {"port", 6550},
            {"unit_id", 1},
            {"timeout_ms", 100},
        },
    };

    const RunResult r = runService(
        writeConfig(baseConfig(vision.baseUrl().mid(QString("http://").size()), cranes)));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_EQ(vision.scanCallCount(), 0);
    EXPECT_TRUE(waitUntil([&]() { return goodCrane.isManualMode(); }, 1000));
}

TEST_F(BinScanOrchestratorServiceTest, T05_MultiplePollsBeforeReady) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start(40, 260, 120, 40)) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["crane_poll_interval_ms"] = 100;
    cfg["crane_wait_timeout_ms"] = 1500;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_GE(countOccurrences(r.stderrText, "crane poll round"), 3);
}

TEST_F(BinScanOrchestratorServiceTest, T06_CraneWaitTimeout) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start(40, 700, 700, 40)) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["crane_poll_interval_ms"] = 100;
    cfg["crane_wait_timeout_ms"] = 1000;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("crane wait timeout")) << qPrintable(r.stderrText);
}

TEST_F(BinScanOrchestratorServiceTest, T07_ScanStartsOnFirstAttempt) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const RunResult r = runService(writeConfig(baseConfig(
        vision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(crane, "crane_a")})));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(vision.scanCallCount(), 1);
    EXPECT_EQ(vision.lastScanBody().value("cmd").toString(), "scan");
    EXPECT_EQ(vision.lastScanBody().value("id").toInt(), 15);
}

TEST_F(BinScanOrchestratorServiceTest, T08_ScanStartRetryThenSuccess) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanError("busy");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const RunResult r = runService(writeConfig(baseConfig(
        vision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(crane, "crane_a")})));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(vision.scanCallCount(), 2);
    EXPECT_TRUE(r.stderrText.contains("scan start retry"));
}

TEST_F(BinScanOrchestratorServiceTest, T09_ScanStartRetriesExhausted) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanError("busy");
    vision.enqueueScanError("busy");
    vision.enqueueScanError("busy");

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_start_retry_count"] = 2;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_EQ(vision.scanCallCount(), 3);
    EXPECT_TRUE(r.stderrText.contains("scan start failed"));
}

TEST_F(BinScanOrchestratorServiceTest, T09A_ProxyTimeoutAppliesToSingleScanRequest) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanHang();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_start_retry_count"] = 0;
    cfg["scan_request_timeout_ms"] = 500;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_EQ(vision.lastScanBody().value("cmd").toString(), "scan");
    EXPECT_FALSE(vision.lastScanBody().contains("timeout"))
        << "timeout must be passed via proxy options, not 3dvision command params";
    EXPECT_TRUE(r.stderrText.contains("ETIMEDOUT") || r.stderrText.contains("timeout"));
}

TEST_F(BinScanOrchestratorServiceTest, LoginEmptyTokenClosesVisionDriver) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("");

    const QString driverExePath = visionDriverExecutablePath();
    ASSERT_TRUE(QFileInfo::exists(driverExePath));

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()), QJsonArray{});
    cfg["cranes"] = QJsonArray{
        QJsonObject{
            {"id", "unused"},
            {"host", "127.0.0.1"},
            {"port", 502},
            {"unit_id", 1},
            {"timeout_ms", 1000},
        },
    };

    const RunResult r = runService(writeConfig(cfg, "config_login_empty_token.json"));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("vision login returned empty token")) << qPrintable(r.stderrText);
    EXPECT_TRUE(waitForNoProcessByExecutablePath(driverExePath, 1000));
    killProcessesByExecutablePath(driverExePath);
}

TEST_F(BinScanOrchestratorServiceTest, T10_FirstFreshLogCompletesScan) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const RunResult r = runService(writeConfig(baseConfig(
        vision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(crane, "crane_a")})));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(readUtf8File(resultPath()).toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError);
    EXPECT_EQ(doc.object().value("completionChannel").toString(), "poll");
    EXPECT_EQ(vision.lastLogCallCount(), 1);
    EXPECT_EQ(vision.lastLogBody().value("id").toInt(), 15);
}

TEST_F(BinScanOrchestratorServiceTest, T11_StaleLogIgnoredUntilFreshLogAppears) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogOlderThanNow();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    const RunResult r = runService(writeConfig(baseConfig(
        vision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(crane, "crane_a")})));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_GE(vision.lastLogCallCount(), 2);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(readUtf8File(resultPath()).toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError);
    EXPECT_EQ(doc.object().value("visionLog").toObject().value("pointCloudPath").toString(),
              "/tmp/pc/latest.pcd");
}

TEST_F(BinScanOrchestratorServiceTest, T12_PollFailLimitReached) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogError("network");
    vision.enqueueLastLogError("network");
    vision.enqueueLastLogError("network");

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_poll_fail_limit"] = 3;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("scan poll fail limit"));
}

TEST_F(BinScanOrchestratorServiceTest, T12A_PollTimeoutCountsAsFailure) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogHang();
    vision.enqueueLastLogHang();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_request_timeout_ms"] = 500;
    cfg["scan_poll_fail_limit"] = 2;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("scan poll fail limit") || r.stderrText.contains("timeout"));
}

TEST_F(BinScanOrchestratorServiceTest, T13_TotalScanTimeout) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    for (int i = 0; i < 16; ++i) {
        vision.enqueueLastLogOlderThanNow();
    }

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_timeout_ms"] = 1000;
    cfg["scan_poll_interval_ms"] = 200;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("scan timeout"));
}

TEST_F(BinScanOrchestratorServiceTest, T14_OnErrorSetManualRestoresManualMode) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanError("fail");
    vision.enqueueScanError("fail");
    vision.enqueueScanError("fail");

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start()) << qPrintable(crane.error());

    QJsonObject cfg = baseConfig(vision.baseUrl().mid(QString("http://").size()),
                                 QJsonArray{craneConfig(crane, "crane_a")});
    cfg["scan_start_retry_count"] = 2;
    cfg["on_error_set_manual"] = true;

    const RunResult r = runService(writeConfig(cfg));
    EXPECT_TRUE(r.finished);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(waitUntil([&]() { return crane.isManualMode(); }, 1000));
}

TEST_F(BinScanOrchestratorServiceTest, T15_ResultFileWritten) {
    FakeVisionServer vision;
    ASSERT_TRUE(vision.isListening());
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = PlcCraneSimHandle::create();
    ASSERT_TRUE(crane.start());

    const RunResult r = runService(writeConfig(baseConfig(
        vision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(crane, "crane_a")})));
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(QFileInfo::exists(resultPath()));

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(readUtf8File(resultPath()).toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError);
    const QJsonObject obj = doc.object();
    EXPECT_TRUE(obj.contains("vesselId"));
    EXPECT_TRUE(obj.contains("status"));
    EXPECT_TRUE(obj.contains("scanDurationMs"));
    EXPECT_TRUE(obj.contains("visionLog"));
}

TEST_F(BinScanOrchestratorServiceTest, T16_ExitCodeContractStable) {
    FakeVisionServer successVision;
    ASSERT_TRUE(successVision.isListening());
    successVision.enqueueLoginDone("token-1");
    successVision.enqueueScanDone();
    successVision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle successCrane = PlcCraneSimHandle::create();
    ASSERT_TRUE(successCrane.start()) << qPrintable(successCrane.error());
    const RunResult success = runService(writeConfig(baseConfig(
        successVision.baseUrl().mid(QString("http://").size()),
        QJsonArray{craneConfig(successCrane, "crane_success")}),
        "config_success.json"));

    FakeVisionServer failVision;
    ASSERT_TRUE(failVision.isListening());
    failVision.enqueueLoginDone("token-1");
    failVision.enqueueScanError("busy");
    failVision.enqueueScanError("busy");
    failVision.enqueueScanError("busy");

    PlcCraneSimHandle failCrane = PlcCraneSimHandle::create();
    ASSERT_TRUE(failCrane.start()) << qPrintable(failCrane.error());
    QJsonObject failCfg = baseConfig(failVision.baseUrl().mid(QString("http://").size()),
                                     QJsonArray{craneConfig(failCrane, "crane_fail")});
    failCfg["scan_start_retry_count"] = 2;
    const RunResult failure = runService(writeConfig(failCfg, "config_failure.json"));

    EXPECT_TRUE(success.finished);
    EXPECT_TRUE(failure.finished);
    EXPECT_EQ(success.exitCode, 0);
    EXPECT_NE(failure.exitCode, 0);
}

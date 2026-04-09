#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "stdiolink_server/scanner/driver_manager_scanner.h"

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
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString directPath = appDir + "/" + baseName + exeSuffix();
    if (QFileInfo::exists(directPath)) {
        return directPath;
    }

    const QDir binDir(appDir);
    const QString runtimePath = binDir.absoluteFilePath(
        "../data_root/drivers/" + baseName + "/" + baseName + exeSuffix());
    if (QFileInfo::exists(runtimePath)) {
        return QDir::cleanPath(runtimePath);
    }
    return directPath;
}

bool copyExecutable(const QString& fromPath, const QString& toPath) {
    QFile::remove(toPath);
    if (!QFile::copy(fromPath, toPath)) {
        return false;
    }
    const QFileDevice::Permissions perms =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    return QFile::setPermissions(toPath, perms);
}

} // namespace

class DriverManagerScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
        driversDir = tmpDir.path() + "/drivers";
        ASSERT_TRUE(QDir().mkpath(driversDir));

        metaDriverPath = testBinaryPath("test_meta_driver");
        failDriverPath = testBinaryPath("test_driver");
        ASSERT_TRUE(QFileInfo::exists(metaDriverPath));
        ASSERT_TRUE(QFileInfo::exists(failDriverPath));
        scanRobotDriverPath = testBinaryPath("stdio.drv.3d_scan_robot");
        pqwAnalogOutputDriverPath = testBinaryPath("stdio.drv.pqw_analog_output");
        tempScannerDriverPath = testBinaryPath("stdio.drv.3d_temp_scanner");
        laserRadarDriverPath = testBinaryPath("stdio.drv.3d_laser_radar");
    }

    QString createDriverDirWithBinary(const QString& name, const QString& sourceBinary) {
        const QString dir = driversDir + "/" + name;
        EXPECT_TRUE(QDir().mkpath(dir));
        const QString targetBin = dir + "/stdio.drv.driver_under_test" + exeSuffix();
        EXPECT_TRUE(copyExecutable(sourceBinary, targetBin));
        return dir;
    }

    QTemporaryDir tmpDir;
    QString driversDir;
    QString metaDriverPath;
    QString failDriverPath;
    QString scanRobotDriverPath;
    QString pqwAnalogOutputDriverPath;
    QString tempScannerDriverPath;
    QString laserRadarDriverPath;
};

TEST_F(DriverManagerScannerTest, NonExistentDirectory) {
    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan("/path/does/not/exist", false, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scanned, 0);
}

TEST_F(DriverManagerScannerTest, SkipFailedDirectories) {
    ASSERT_TRUE(QDir().mkpath(driversDir + "/broken.failed"));

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    (void)scanner.scan(driversDir, false, &stats);

    EXPECT_EQ(stats.skippedFailed, 1);
    EXPECT_EQ(stats.scanned, 0);
}

TEST_F(DriverManagerScannerTest, ExportMissingMetaAndLoadSuccess) {
    const QString dir = createDriverDirWithBinary("good", metaDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 1);
    EXPECT_EQ(stats.newlyFailed, 0);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("test-meta-driver"));
}

TEST_F(DriverManagerScannerTest, ExportFailureMarksDirectoryFailed) {
    const QString dir = createDriverDirWithBinary("bad", failDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.newlyFailed, 1);
    EXPECT_FALSE(QFileInfo::exists(dir));
    EXPECT_TRUE(QFileInfo::exists(dir + ".failed"));
}

TEST_F(DriverManagerScannerTest, RefreshFailureKeepsOldMeta) {
    const QString dir = createDriverDirWithBinary("refresh", metaDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats firstStats;
    const auto first = scanner.scan(driversDir, false, &firstStats);
    ASSERT_EQ(first.size(), 1);
    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));

    const QString oldBin = dir + "/stdio.drv.driver_under_test" + exeSuffix();
    ASSERT_TRUE(QFile::remove(oldBin));
    ASSERT_TRUE(copyExecutable(failDriverPath, oldBin));

    DriverManagerScanner::ScanStats secondStats;
    const auto second = scanner.scan(driversDir, true, &secondStats);

    EXPECT_EQ(secondStats.scanned, 1);
    EXPECT_EQ(secondStats.newlyFailed, 0);
    EXPECT_EQ(secondStats.updated, 1);
    ASSERT_EQ(second.size(), 1);
    EXPECT_TRUE(second.contains("test-meta-driver"));
    EXPECT_TRUE(QFileInfo::exists(dir));
    EXPECT_FALSE(QFileInfo::exists(dir + ".failed"));
}

TEST_F(DriverManagerScannerTest, InvalidMetaIsSkippedWithoutMarkingFailed) {
    const QString dir = createDriverDirWithBinary("invalid-meta", metaDriverPath);
    QFile metaFile(dir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(metaFile.write("{bad-json"), 0);
    metaFile.close();

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 0);
    EXPECT_EQ(stats.newlyFailed, 0);
    EXPECT_TRUE(QFileInfo::exists(dir));
    EXPECT_FALSE(QFileInfo::exists(dir + ".failed"));
}

TEST_F(DriverManagerScannerTest, ValidMetaButNonConformingExeIsSkipped) {
    const QString dir = createDriverDirWithBinary("no-prefix", metaDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats1;
    const auto first = scanner.scan(driversDir, false, &stats1);
    ASSERT_EQ(first.size(), 1);
    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));

    // Replace prefix-conforming exe with a non-conforming one
    const QString goodBin = dir + "/stdio.drv.driver_under_test" + exeSuffix();
    ASSERT_TRUE(QFile::remove(goodBin));
    const QString badBin = dir + "/driver_no_prefix" + exeSuffix();
    ASSERT_TRUE(copyExecutable(metaDriverPath, badBin));

    DriverManagerScanner::ScanStats stats2;
    const auto second = scanner.scan(driversDir, false, &stats2);

    EXPECT_TRUE(second.isEmpty());
    EXPECT_EQ(stats2.scanned, 1);
    EXPECT_EQ(stats2.updated, 0);
    EXPECT_EQ(stats2.newlyFailed, 0);
    EXPECT_TRUE(QFileInfo::exists(dir));
}

TEST_F(DriverManagerScannerTest, ExportMetaFor3DScanRobotDriver) {
    if (!QFileInfo::exists(scanRobotDriverPath)) {
        GTEST_SKIP() << "stdio.drv.3d_scan_robot binary is not available in the test output directory";
    }

    const QString dir = createDriverDirWithBinary("scan-robot", scanRobotDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 1);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("stdio.drv.3d_scan_robot"));

    QFile metaFile(dir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::ReadOnly));
    const auto metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    ASSERT_TRUE(metaDoc.isObject());
    const auto commands = metaDoc.object().value("commands").toArray();

    QStringList commandNames;
    for (const auto& value : commands) {
        commandNames.append(value.toObject().value("name").toString());
    }

    EXPECT_TRUE(commandNames.contains("scan_line"));
    EXPECT_TRUE(commandNames.contains("scan_frame"));
    EXPECT_TRUE(commandNames.contains("query"));
    EXPECT_TRUE(commandNames.contains("interrupt_test"));
    EXPECT_FALSE(commandNames.contains("get_line"));
    EXPECT_FALSE(commandNames.contains("get_frame"));
    EXPECT_FALSE(commandNames.contains("insert_test"));
    EXPECT_FALSE(commandNames.contains("wait"));
}

TEST_F(DriverManagerScannerTest, ExportMetaForPqwAnalogOutputDriver) {
    if (!QFileInfo::exists(pqwAnalogOutputDriverPath)) {
        GTEST_SKIP() << "stdio.drv.pqw_analog_output binary is not available in the test output directory";
    }

    const QString dir = createDriverDirWithBinary("pqw-analog-output", pqwAnalogOutputDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 1);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("stdio.drv.pqw_analog_output"));

    QFile metaFile(dir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::ReadOnly));
    const auto metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    ASSERT_TRUE(metaDoc.isObject());
    const auto commands = metaDoc.object().value("commands").toArray();

    QStringList commandNames;
    for (const auto& value : commands) {
        commandNames.append(value.toObject().value("name").toString());
    }

    EXPECT_TRUE(commandNames.contains("get_config"));
    EXPECT_TRUE(commandNames.contains("write_output"));
    EXPECT_TRUE(commandNames.contains("write_outputs"));
    EXPECT_TRUE(commandNames.contains("clear_outputs"));
}

TEST_F(DriverManagerScannerTest, ExportMetaFor3DTempScannerDriver) {
    if (!QFileInfo::exists(tempScannerDriverPath)) {
        GTEST_SKIP() << "stdio.drv.3d_temp_scanner binary is not available in the test output directory";
    }

    const QString dir = createDriverDirWithBinary("3d-temp-scanner", tempScannerDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 1);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("stdio.drv.3d_temp_scanner"));

    QFile metaFile(dir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::ReadOnly));
    const auto metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    ASSERT_TRUE(metaDoc.isObject());
    const auto commands = metaDoc.object().value("commands").toArray();

    QStringList commandNames;
    for (const auto& value : commands) {
        commandNames.append(value.toObject().value("name").toString());
    }

    EXPECT_TRUE(commandNames.contains("status"));
    EXPECT_TRUE(commandNames.contains("capture"));
    EXPECT_FALSE(commandNames.contains("test"));
    EXPECT_FALSE(commandNames.contains("get_board_temp"));
}

TEST_F(DriverManagerScannerTest, ExportMetaFor3DLaserRadarDriver) {
    if (!QFileInfo::exists(laserRadarDriverPath)) {
        GTEST_SKIP() << "stdio.drv.3d_laser_radar binary is not available in the test output directory";
    }

    const QString dir = createDriverDirWithBinary("3d-laser-radar", laserRadarDriverPath);

    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    const auto result = scanner.scan(driversDir, false, &stats);

    ASSERT_TRUE(QFileInfo::exists(dir + "/driver.meta.json"));
    EXPECT_EQ(stats.scanned, 1);
    EXPECT_EQ(stats.updated, 1);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("stdio.drv.3d_laser_radar"));

    QFile metaFile(dir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::ReadOnly));
    const auto metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    ASSERT_TRUE(metaDoc.isObject());
    const auto commands = metaDoc.object().value("commands").toArray();

    QStringList commandNames;
    for (const auto& value : commands) {
        commandNames.append(value.toObject().value("name").toString());
    }

    EXPECT_TRUE(commandNames.contains("status"));
    EXPECT_TRUE(commandNames.contains("scan_field"));
    EXPECT_TRUE(commandNames.contains("get_data"));
    EXPECT_TRUE(commandNames.contains("move_x"));
    EXPECT_TRUE(commandNames.contains("calib_lidar"));
    EXPECT_FALSE(commandNames.contains("set_scan_mode"));
    EXPECT_FALSE(commandNames.contains("scan_to_angle"));
    EXPECT_FALSE(commandNames.contains("get_scan_line"));
}

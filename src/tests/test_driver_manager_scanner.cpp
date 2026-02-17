#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    return QCoreApplication::applicationDirPath() + "/" + baseName + exeSuffix();
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

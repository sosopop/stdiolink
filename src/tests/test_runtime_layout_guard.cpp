#include "runtime_layout_guard.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace stdiolink::tests {

TEST(RuntimeLayoutGuardTest, AcceptsRuntimeBinLayoutWithDataRoot) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QDir root(tmpDir.path());
    ASSERT_TRUE(root.mkpath("runtime_release/bin"));
    ASSERT_TRUE(root.mkpath("runtime_release/data_root/drivers"));
    ASSERT_TRUE(root.mkpath("runtime_release/data_root/services"));

    const QString exePath = root.absoluteFilePath("runtime_release/bin/stdiolink_tests");
    QFile exeFile(exePath);
    ASSERT_TRUE(exeFile.open(QIODevice::WriteOnly));
    exeFile.close();

    const RuntimeLayoutCheckResult result = validateTestRuntimeLayout(
        exePath, root.absoluteFilePath("runtime_release/bin"));

    EXPECT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_TRUE(result.message.isEmpty());
}

TEST(RuntimeLayoutGuardTest, RejectsRawBuildOutputDirectory) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QDir root(tmpDir.path());
    ASSERT_TRUE(root.mkpath("build/release"));

    const QString exePath = root.absoluteFilePath("build/release/stdiolink_tests");
    QFile exeFile(exePath);
    ASSERT_TRUE(exeFile.open(QIODevice::WriteOnly));
    exeFile.close();

    const RuntimeLayoutCheckResult result = validateTestRuntimeLayout(
        exePath, root.absoluteFilePath("build/release"));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("runtime layout like <runtime_root>/bin/"));
    EXPECT_TRUE(result.message.contains("build\\release\\stdiolink_tests")
                || result.message.contains("build/release/stdiolink_tests"));
}

TEST(RuntimeLayoutGuardTest, RejectsMissingRuntimeDataRootEntries) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QDir root(tmpDir.path());
    ASSERT_TRUE(root.mkpath("runtime_release/bin"));
    ASSERT_TRUE(root.mkpath("runtime_release/data_root/drivers"));

    const QString exePath = root.absoluteFilePath("runtime_release/bin/stdiolink_tests");
    QFile exeFile(exePath);
    ASSERT_TRUE(exeFile.open(QIODevice::WriteOnly));
    exeFile.close();

    const RuntimeLayoutCheckResult result = validateTestRuntimeLayout(
        exePath, root.absoluteFilePath("runtime_release/bin"));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("data_root/services/"));
    EXPECT_TRUE(result.message.contains("Do not run raw build outputs"));
}

}  // namespace stdiolink::tests

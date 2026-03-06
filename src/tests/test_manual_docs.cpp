#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>

namespace {

QString findRepoRoot() {
    const QDir binDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        binDir.absoluteFilePath("../../.."),
        binDir.absoluteFilePath("../.."),
        QDir::currentPath(),
    };
    for (const QString& candidate : candidates) {
        if (QFile::exists(QDir(candidate).filePath("doc/manual/07-console/README.md"))) {
            return QDir(candidate).absolutePath();
        }
    }
    return QString();
}

QString readTextFile(const QString& relativePath) {
    const QString repoRoot = findRepoRoot();
    EXPECT_FALSE(repoRoot.isEmpty());
    QFile file(QDir(repoRoot).filePath(relativePath));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << relativePath.toStdString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

TEST(ManualDocs, T01_ConsoleManualContainsNormalizedExamples) {
    const QString consoleDoc = readTextFile("doc/manual/07-console/README.md");
    EXPECT_TRUE(consoleDoc.contains("servers[0].host"));
    EXPECT_TRUE(consoleDoc.contains("tags[]"));
    EXPECT_TRUE(consoleDoc.contains("--units[0].id=1"));
}

TEST(ManualDocs, T02_DriverLabManualExplainsDisplayBoundary) {
    const QString driverlabDoc = readTextFile("doc/manual/12-webui/driverlab.md");
    EXPECT_TRUE(driverlabDoc.contains("argv token"));
    EXPECT_TRUE(driverlabDoc.contains("JSON"));
}

TEST(ManualDocs, T03_BestPracticesContainsExplicitPathRecommendation) {
    const QString doc = readTextFile("doc/manual/08-best-practices.md");
    EXPECT_TRUE(doc.contains("--units[0].id=1"));
    EXPECT_TRUE(doc.contains("错误处理"));
    EXPECT_TRUE(doc.contains("config.schema.json"));
}

TEST(ManualDocs, T04_TroubleshootingContainsShellCompatibilityNotes) {
    const QString doc = readTextFile("doc/manual/09-troubleshooting.md");
    EXPECT_TRUE(doc.contains("PowerShell 5.1"));
    EXPECT_TRUE(doc.contains("expected string"));
    EXPECT_TRUE(doc.contains("expected array"));
    EXPECT_TRUE(doc.contains("Driver 启动失败"));
    EXPECT_TRUE(doc.contains("DriverBusyError"));
}

TEST(ManualDocs, T05_MigrationContainsOldToNewExamples) {
    const QString consoleDoc = readTextFile("doc/manual/07-console/README.md");
    const QString troubleshootDoc = readTextFile("doc/manual/09-troubleshooting.md");
    const QString joined = consoleDoc + "\n" + troubleshootDoc;
    EXPECT_TRUE(joined.contains("--units=[{\"id\":1}]"));
    EXPECT_TRUE(joined.contains("--units[0].id=1"));
}

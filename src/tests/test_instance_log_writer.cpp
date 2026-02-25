#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "stdiolink_server/manager/instance_log_writer.h"

using namespace stdiolink_server;

namespace {

QStringList readLogLines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList lines;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty()) lines.append(line);
    }
    return lines;
}

const QRegularExpression kTimestampRe(
    R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \| .+$)");

} // namespace

TEST(InstanceLogWriterTest, StdoutLineWithTimestamp) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("hello world\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(kTimestampRe.match(lines[0]).hasMatch());
    EXPECT_TRUE(lines[0].contains("hello world"));
    EXPECT_FALSE(lines[0].contains("[stderr]"));
}

TEST(InstanceLogWriterTest, StderrLineWithPrefix) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStderr("some warning\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("[stderr] some warning"));
}

TEST(InstanceLogWriterTest, MixedStdoutStderr) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("out1\n");
        writer.appendStderr("err1\n");
        writer.appendStdout("out2\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_TRUE(lines[0].contains("out1"));
    EXPECT_FALSE(lines[0].contains("[stderr]"));
    EXPECT_TRUE(lines[1].contains("[stderr] err1"));
    EXPECT_TRUE(lines[2].contains("out2"));
    EXPECT_FALSE(lines[2].contains("[stderr]"));
}

TEST(InstanceLogWriterTest, IncompleteLineBuffered) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("hel");
        EXPECT_TRUE(readLogLines(logPath).isEmpty());
        writer.appendStdout("lo\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("hello"));
}

TEST(InstanceLogWriterTest, MultipleLinesSingleChunk) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("line1\nline2\nline3\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 3);
    for (const auto& line : lines) {
        EXPECT_TRUE(kTimestampRe.match(line).hasMatch());
    }
}

TEST(InstanceLogWriterTest, FileRotation) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath, 1024, 2);
        const QByteArray chunk = QByteArray(200, 'x') + "\n";
        for (int i = 0; i < 50; ++i) {
            writer.appendStdout(chunk);
        }
    }

    // spdlog rotation: test.log → test.1.log (index inserted before extension)
    const QString rotatedPath = tmpDir.path() + "/test.1.log";
    EXPECT_TRUE(QFileInfo::exists(rotatedPath))
        << "Rotated file not found: " << qPrintable(rotatedPath);
}

TEST(InstanceLogWriterTest, DestructorFlushesIncompleteBuffer) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("incomplete");  // no newline
    }  // destructor flushes

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("incomplete"));
}

TEST(InstanceLogWriterTest, BufferOverflowForcesFlush) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        // 写入超过 1MB 无换行数据，触发强制刷出
        const QByteArray chunk(128 * 1024, 'A');  // 128KB
        for (int i = 0; i < 9; ++i) {
            writer.appendStdout(chunk);
        }
        // 此时缓冲区 > 1MB，应已强制刷出
        const auto lines = readLogLines(logPath);
        EXPECT_GE(lines.size(), 1) << "Buffer overflow should force flush";
    }
}

TEST(InstanceLogWriterTest, EmptyLinesPreserved) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("line1\n\nline3\n");
    }

    QFile file(logPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QStringList allLines;
    while (!file.atEnd()) {
        allLines.append(QString::fromUtf8(file.readLine()).trimmed());
    }
    // 应有 3 行（含空行的时间戳行）
    int nonEmpty = 0;
    for (const auto& l : allLines) {
        if (!l.isEmpty()) nonEmpty++;
    }
    EXPECT_EQ(nonEmpty, 3);
}

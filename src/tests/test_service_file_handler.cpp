#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "stdiolink_server/http/service_file_handler.h"

using namespace stdiolink_server;

namespace {

bool writeText(const QString& path, const QString& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return file.error() == QFile::NoError;
}

} // namespace

// --- Path Safety Tests ---

TEST(ServiceFileHandlerTest, SafePathNormalFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/index.js", "ok"));
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "index.js"));
}

TEST(ServiceFileHandlerTest, SafePathSubdirectoryFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir().mkpath(tmp.path() + "/lib");
    ASSERT_TRUE(writeText(tmp.path() + "/lib/utils.js", "ok"));
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "lib/utils.js"));
}

TEST(ServiceFileHandlerTest, UnsafePathEmpty) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), ""));
}

TEST(ServiceFileHandlerTest, UnsafePathSimpleTraversal) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "../etc/passwd"));
}

TEST(ServiceFileHandlerTest, UnsafePathNestedTraversal) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "foo/../../etc/passwd"));
}

TEST(ServiceFileHandlerTest, UnsafePathMixedTraversal) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "foo/./bar/../../../etc/passwd"));
}

TEST(ServiceFileHandlerTest, UnsafePathAbsolute) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "/etc/passwd"));
}

TEST(ServiceFileHandlerTest, UnsafePathContainsDotDot) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "foo/../bar"));
}

TEST(ServiceFileHandlerTest, SafePathDotDotInFileName) {
    // "..hidden" is a legal file name — should NOT be rejected
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/..hidden", "ok"));
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "..hidden"));
}

TEST(ServiceFileHandlerTest, UnsafePathMultiLevelBacktrack) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "foo/bar/../../baz"));
}

TEST(ServiceFileHandlerTest, SafePathCurrentDirPrefix) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/index.js", "ok"));
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "./index.js"));
}

TEST(ServiceFileHandlerTest, SafePathDeepSubdirectory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir().mkpath(tmp.path() + "/a/b/c");
    ASSERT_TRUE(writeText(tmp.path() + "/a/b/c/d.js", "ok"));
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "a/b/c/d.js"));
}

TEST(ServiceFileHandlerTest, UnsafePathSymlink) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    // Create a symlink pointing outside
    const QString linkPath = tmp.path() + "/link_outside";
    QFile::link("/tmp", linkPath);
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "link_outside/passwd"));
}

// --- resolveSafePath Tests ---

TEST(ServiceFileHandlerTest, ResolveSafePathSuccess) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/index.js", "ok"));

    QString error;
    const QString result = ServiceFileHandler::resolveSafePath(tmp.path(), "index.js", error);
    EXPECT_FALSE(result.isEmpty());
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(result.endsWith("/index.js"));
}

TEST(ServiceFileHandlerTest, ResolveSafePathFailure) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QString error;
    const QString result = ServiceFileHandler::resolveSafePath(tmp.path(), "../etc/passwd", error);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

// --- Atomic Write Tests ---

TEST(ServiceFileHandlerTest, AtomicWriteNewFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = tmp.path() + "/new_file.txt";

    QString error;
    ASSERT_TRUE(ServiceFileHandler::atomicWrite(path, "hello world", error));
    EXPECT_TRUE(error.isEmpty());

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), "hello world");
}

TEST(ServiceFileHandlerTest, AtomicWriteOverwrite) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = tmp.path() + "/existing.txt";
    ASSERT_TRUE(writeText(path, "old content"));

    QString error;
    ASSERT_TRUE(ServiceFileHandler::atomicWrite(path, "new content", error));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), "new content");
}

TEST(ServiceFileHandlerTest, AtomicWriteNoTmpResidue) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = tmp.path() + "/clean.txt";

    QString error;
    ASSERT_TRUE(ServiceFileHandler::atomicWrite(path, "data", error));

    // Check no .tmp files remain
    QDir dir(tmp.path());
    const QStringList entries = dir.entryList(QDir::Files);
    for (const QString& entry : entries) {
        EXPECT_FALSE(entry.contains(".tmp")) << qPrintable("Residual tmp file: " + entry);
    }
}

TEST(ServiceFileHandlerTest, AtomicWriteFailsForMissingDir) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = tmp.path() + "/nonexistent_dir/file.txt";

    QString error;
    EXPECT_FALSE(ServiceFileHandler::atomicWrite(path, "data", error));
    EXPECT_FALSE(error.isEmpty());
}

// --- File Listing Tests ---

TEST(ServiceFileHandlerTest, ListFilesBasic) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/manifest.json", ""));
    ASSERT_TRUE(writeText(tmp.path() + "/index.js", "ok"));
    QDir().mkpath(tmp.path() + "/lib");
    ASSERT_TRUE(writeText(tmp.path() + "/lib/utils.js", "ok"));

    const auto files = ServiceFileHandler::listFiles(tmp.path());
    ASSERT_EQ(files.size(), 3);

    // Sorted by path
    EXPECT_EQ(files[0].path, "index.js");
    EXPECT_EQ(files[1].path, "lib/utils.js");
    EXPECT_EQ(files[2].path, "manifest.json");
}

TEST(ServiceFileHandlerTest, ListFilesSkipsSymlinks) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ASSERT_TRUE(writeText(tmp.path() + "/real.txt", "ok"));
    QFile::link("/tmp", tmp.path() + "/link");

    const auto files = ServiceFileHandler::listFiles(tmp.path());
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].name, "real.txt");
}

// --- File Type Inference Tests ---

TEST(ServiceFileHandlerTest, InferFileType) {
    EXPECT_EQ(ServiceFileHandler::inferFileType("manifest.json"), "json");
    EXPECT_EQ(ServiceFileHandler::inferFileType("index.js"), "javascript");
    EXPECT_EQ(ServiceFileHandler::inferFileType("main.ts"), "typescript");
    EXPECT_EQ(ServiceFileHandler::inferFileType("README.md"), "markdown");
    EXPECT_EQ(ServiceFileHandler::inferFileType("notes.txt"), "text");
    EXPECT_EQ(ServiceFileHandler::inferFileType("config.yaml"), "yaml");
    EXPECT_EQ(ServiceFileHandler::inferFileType("config.yml"), "yaml");
    EXPECT_EQ(ServiceFileHandler::inferFileType("unknown.bin"), "text");
}

// --- Core Files Tests ---

TEST(ServiceFileHandlerTest, CoreFilesContainsExpected) {
    const auto& core = ServiceFileHandler::coreFiles();
    EXPECT_TRUE(core.contains("manifest.json"));
    EXPECT_TRUE(core.contains("index.js"));
    EXPECT_TRUE(core.contains("config.schema.json"));
    EXPECT_EQ(core.size(), 3);
}

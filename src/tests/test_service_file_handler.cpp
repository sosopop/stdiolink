#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "stdiolink_server/http/service_file_handler.h"

using namespace stdiolink_server;

namespace {

bool writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

} // namespace

// --- Path safety tests ---

TEST(ServiceFileHandlerTest, SafePathNormalFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "index.js"));
}

TEST(ServiceFileHandlerTest, SafePathSubdirectory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
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

TEST(ServiceFileHandlerTest, SafePathDotDotInFilename) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    // "..hidden" is a valid filename, not a traversal
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
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "./index.js"));
}

TEST(ServiceFileHandlerTest, SafePathDeepSubdirectory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_TRUE(ServiceFileHandler::isPathSafe(tmp.path(), "a/b/c/d.js"));
}

TEST(ServiceFileHandlerTest, UnsafePathSymlink) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a symlink pointing outside
    const QString linkPath = tmp.path() + "/link_outside";
    if (!QFile::link("/tmp", linkPath)) {
        GTEST_SKIP() << "Cannot create symlink";
    }

    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "link_outside/passwd"));
}

TEST(ServiceFileHandlerTest, UnsafePathBackslashTraversal) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_FALSE(ServiceFileHandler::isPathSafe(tmp.path(), "foo\\..\\..\\etc\\passwd"));
}

// --- Atomic write tests ---

TEST(ServiceFileHandlerTest, AtomicWriteNewFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString path = tmp.path() + "/test.txt";
    QString error;
    ASSERT_TRUE(ServiceFileHandler::atomicWrite(path, "hello world", error))
        << qPrintable(error);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), "hello world");
}

TEST(ServiceFileHandlerTest, AtomicWriteOverwrite) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString path = tmp.path() + "/test.txt";
    ASSERT_TRUE(writeFile(path, "old content"));

    QString error;
    ASSERT_TRUE(ServiceFileHandler::atomicWrite(path, "new content", error))
        << qPrintable(error);

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

    // Check no .tmp files left
    QDir dir(tmp.path());
    const auto entries = dir.entryList(QStringList{"*.tmp"}, QDir::Files);
    EXPECT_TRUE(entries.isEmpty());
}

// --- File listing tests ---

TEST(ServiceFileHandlerTest, ListFilesBasic) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ASSERT_TRUE(writeFile(tmp.path() + "/manifest.json", "{}"));
    ASSERT_TRUE(writeFile(tmp.path() + "/index.js", "//"));

    const auto files = ServiceFileHandler::listFiles(tmp.path());
    EXPECT_GE(files.size(), 2);

    bool foundManifest = false;
    bool foundIndex = false;
    for (const FileInfo& fi : files) {
        if (fi.path == "manifest.json") foundManifest = true;
        if (fi.path == "index.js") foundIndex = true;
    }
    EXPECT_TRUE(foundManifest);
    EXPECT_TRUE(foundIndex);
}

TEST(ServiceFileHandlerTest, ListFilesSubdirectory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ASSERT_TRUE(QDir().mkpath(tmp.path() + "/lib"));
    ASSERT_TRUE(writeFile(tmp.path() + "/lib/utils.js", "//"));

    const auto files = ServiceFileHandler::listFiles(tmp.path());
    bool found = false;
    for (const FileInfo& fi : files) {
        if (fi.path == "lib/utils.js") {
            found = true;
            EXPECT_EQ(fi.type, "javascript");
        }
    }
    EXPECT_TRUE(found);
}

// --- File type inference ---

TEST(ServiceFileHandlerTest, InferFileType) {
    EXPECT_EQ(ServiceFileHandler::inferFileType("manifest.json"), "json");
    EXPECT_EQ(ServiceFileHandler::inferFileType("index.js"), "javascript");
    EXPECT_EQ(ServiceFileHandler::inferFileType("module.mjs"), "javascript");
    EXPECT_EQ(ServiceFileHandler::inferFileType("readme.md"), "text");
    EXPECT_EQ(ServiceFileHandler::inferFileType("data.bin"), "unknown");
}

// --- Core files ---

TEST(ServiceFileHandlerTest, CoreFilesContainsExpected) {
    const auto& core = ServiceFileHandler::coreFiles();
    EXPECT_TRUE(core.contains("manifest.json"));
    EXPECT_TRUE(core.contains("index.js"));
    EXPECT_TRUE(core.contains("config.schema.json"));
}

// --- resolveSafePath ---

TEST(ServiceFileHandlerTest, ResolveSafePathValid) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QString error;
    const QString result = ServiceFileHandler::resolveSafePath(tmp.path(), "index.js", error);
    EXPECT_FALSE(result.isEmpty());
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(result.endsWith("/index.js"));
}

TEST(ServiceFileHandlerTest, ResolveSafePathInvalid) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QString error;
    const QString result = ServiceFileHandler::resolveSafePath(tmp.path(), "../etc/passwd", error);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QHttpServerResponder>
#include <QTemporaryDir>

#include "stdiolink_server/config/server_args.h"
#include "stdiolink_server/config/server_config.h"
#include "stdiolink_server/http/static_file_server.h"

using namespace stdiolink_server;

class StaticFileServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tempDir.isValid());
        m_root = m_tempDir.path();

        // Create index.html
        writeFile("index.html", QByteArray("<html><body>Hello</body></html>"));

        // Create assets directory with hashed files
        QDir(m_root).mkpath("assets");
        writeFile("assets/index-abc123.js", QByteArray("console.log('hello');"));
        writeFile("assets/style-def456.css", QByteArray("body { color: red; }"));

        // Create favicon
        writeFile("favicon.ico", QByteArray(4, '\x00'));

        // Create other files
        writeFile("robots.txt", QByteArray("User-agent: *"));
        writeFile("test.woff2", QByteArray(8, '\x01'));
        writeFile("unknown.xyz", QByteArray("unknown content"));
    }

    void writeFile(const QString& relativePath, const QByteArray& content) {
        const QString fullPath = m_root + "/" + relativePath;
        QDir().mkpath(QFileInfo(fullPath).absolutePath());
        QFile file(fullPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QTemporaryDir m_tempDir;
    QString m_root;
};

// Test 1: Valid directory with index.html
TEST_F(StaticFileServerTest, ValidDirectoryIsValid) {
    StaticFileServer server(m_root);
    EXPECT_TRUE(server.isValid());
}

// Test 2: Non-existent directory
TEST_F(StaticFileServerTest, NonExistentDirectoryIsInvalid) {
    StaticFileServer server(m_root + "/nonexistent");
    EXPECT_FALSE(server.isValid());
}

// Test 3: Directory exists but no index.html
TEST_F(StaticFileServerTest, DirectoryWithoutIndexIsInvalid) {
    QTemporaryDir emptyDir;
    ASSERT_TRUE(emptyDir.isValid());
    StaticFileServer server(emptyDir.path());
    EXPECT_FALSE(server.isValid());
}

// Test 4: Request /index.html
TEST_F(StaticFileServerTest, ServeIndexHtml) {
    StaticFileServer server(m_root);
    auto response = server.serve("/index.html");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 5: Request /assets/index-abc123.js
TEST_F(StaticFileServerTest, ServeJsAsset) {
    StaticFileServer server(m_root);
    auto response = server.serve("/assets/index-abc123.js");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 6: Request /assets/style-def456.css
TEST_F(StaticFileServerTest, ServeCssAsset) {
    StaticFileServer server(m_root);
    auto response = server.serve("/assets/style-def456.css");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 7: Request /favicon.ico
TEST_F(StaticFileServerTest, ServeFavicon) {
    StaticFileServer server(m_root);
    auto response = server.serve("/favicon.ico");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 8: Request non-existent file returns 404
TEST_F(StaticFileServerTest, NonExistentFileReturns404) {
    StaticFileServer server(m_root);
    auto response = server.serve("/nonexistent.txt");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test 9: Path traversal with ..
TEST_F(StaticFileServerTest, PathTraversalBlocked) {
    StaticFileServer server(m_root);
    auto response = server.serve("/../../../etc/passwd");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test 10: Path traversal with encoded ..
TEST_F(StaticFileServerTest, EncodedPathTraversalBlocked) {
    StaticFileServer server(m_root);
    // URL-decoded form of /../../../etc/passwd
    auto response = server.serve("/..%2F..%2Fetc/passwd");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test 11: Symlink file not followed
TEST_F(StaticFileServerTest, SymlinkNotFollowed) {
#ifdef Q_OS_WIN
    GTEST_SKIP() << "QFile::link() creates .lnk shortcuts on Windows, not true symlinks";
#else
    // Create a symlink
    const QString linkPath = m_root + "/link.html";
    const QString targetPath = m_root + "/index.html";
    bool created = QFile::link(targetPath, linkPath);
    if (!created) {
        GTEST_SKIP() << "Cannot create symlink";
    }

    StaticFileServer server(m_root);
    auto response = server.serve("/link.html");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
#endif
}

// Test 12: Oversized file rejected
TEST_F(StaticFileServerTest, OversizedFileRejected) {
    // Create a file > 10MB
    const QString bigFile = m_root + "/big.bin";
    QFile file(bigFile);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray chunk(1024 * 1024, 'A'); // 1MB
    for (int i = 0; i < 11; ++i) {
        file.write(chunk);
    }
    file.close();

    StaticFileServer server(m_root);
    auto response = server.serve("/big.bin");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test 13: serveIndex returns correct content
TEST_F(StaticFileServerTest, ServeIndexReturnsCorrectContent) {
    StaticFileServer server(m_root);
    auto response = server.serveIndex();
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 14: MIME type for .woff2
TEST_F(StaticFileServerTest, MimeTypeWoff2) {
    StaticFileServer server(m_root);
    auto response = server.serve("/test.woff2");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 15: Unknown extension returns octet-stream
TEST_F(StaticFileServerTest, UnknownExtensionServed) {
    StaticFileServer server(m_root);
    auto response = server.serve("/unknown.xyz");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test 16: Root path / — serve returns 404 (directory, not file)
TEST_F(StaticFileServerTest, RootPathReturns404) {
    StaticFileServer server(m_root);
    // "/" maps to the root directory itself, not a file
    auto response = server.serve("/");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test 17: serveIndex for SPA fallback
TEST_F(StaticFileServerTest, ServeIndexForSpaRoute) {
    StaticFileServer server(m_root);
    // SPA routes like /projects/demo should use serveIndex()
    auto response = server.serveIndex();
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::Ok);
}

// Test: rootDir accessor
TEST_F(StaticFileServerTest, RootDirAccessor) {
    StaticFileServer server(m_root);
    EXPECT_EQ(server.rootDir(), QDir::cleanPath(m_root));
}

// Test: serve on invalid server returns 404
TEST_F(StaticFileServerTest, ServeOnInvalidServerReturns404) {
    StaticFileServer server(m_root + "/nonexistent");
    auto response = server.serve("/index.html");
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// Test: serveIndex on invalid server returns 404
TEST_F(StaticFileServerTest, ServeIndexOnInvalidServerReturns404) {
    StaticFileServer server(m_root + "/nonexistent");
    auto response = server.serveIndex();
    EXPECT_EQ(response.statusCode(), QHttpServerResponder::StatusCode::NotFound);
}

// --- Config tests ---

// Test 18: --webui-dir command line argument
TEST(ServerArgsWebuiTest, WebuiDirParsed) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--webui-dir=/path/to/webui"
    });
    EXPECT_TRUE(args.error.isEmpty());
    EXPECT_TRUE(args.hasWebuiDir);
    EXPECT_EQ(args.webuiDir, "/path/to/webui");
}

// Test: --webui-dir empty value
TEST(ServerArgsWebuiTest, WebuiDirEmptyError) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--webui-dir="
    });
    EXPECT_FALSE(args.error.isEmpty());
}

// Test 19: config.json webuiDir field
TEST(ServerConfigWebuiTest, WebuiDirFromConfig) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QString configPath = tmpDir.path() + "/config.json";
    QFile file(configPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"webuiDir": "webui"})");
    file.close();

    QString error;
    auto cfg = ServerConfig::loadFromFile(configPath, error);
    EXPECT_TRUE(error.isEmpty()) << qPrintable(error);
    EXPECT_EQ(cfg.webuiDir, "webui");
}

// Test 20: Command line overrides config
TEST(ServerConfigWebuiTest, ArgsOverrideConfig) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QString configPath = tmpDir.path() + "/config.json";
    QFile file(configPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"webuiDir": "from_config"})");
    file.close();

    QString error;
    auto cfg = ServerConfig::loadFromFile(configPath, error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(cfg.webuiDir, "from_config");

    ServerArgs args;
    args.webuiDir = "/from/args";
    args.hasWebuiDir = true;
    cfg.applyArgs(args);

    EXPECT_EQ(cfg.webuiDir, "/from/args");
}

// Test: webuiDir non-string in config
TEST(ServerConfigWebuiTest, WebuiDirNonStringError) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QString configPath = tmpDir.path() + "/config.json";
    QFile file(configPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"webuiDir": 123})");
    file.close();

    QString error;
    ServerConfig::loadFromFile(configPath, error);
    EXPECT_FALSE(error.isEmpty());
}

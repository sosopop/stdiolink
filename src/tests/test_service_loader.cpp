#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

class ServiceLoaderTest : public ::testing::Test {
protected:
    QString servicePath() {
        QString path = QCoreApplication::applicationDirPath()
                       + "/stdiolink_service";
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        return path;
    }

    void createFile(const QString& path, const QByteArray& content) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(content);
        f.close();
    }

    QByteArray minimalManifest() {
        return R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})";
    }

    QByteArray emptySchema() {
        return R"({})";
    }
};

TEST_F(ServiceLoaderTest, ValidServiceDirExecutesIndexJs) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js",
               "console.log('hello from index.js');\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    EXPECT_TRUE(proc.readAllStandardError().contains("hello from index.js"));
}

TEST_F(ServiceLoaderTest, MissingIndexJsFails) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, MissingConfigSchemaFails) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/index.js", "// ok\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, NonexistentDirFails) {
    QProcess proc;
    proc.start(servicePath(), {"/nonexistent/path"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, HelpWithServiceDir) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js", "// ok\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path(), "--help"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    QByteArray err = proc.readAllStandardError();
    EXPECT_TRUE(err.contains("Test"));  // manifest name
}

TEST_F(ServiceLoaderTest, DumpSchemaWithServiceDir) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js", "// ok\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path(), "--dump-config-schema"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    QByteArray out = proc.readAllStandardOutput();
    EXPECT_FALSE(out.isEmpty());
    QJsonParseError parseErr;
    QJsonDocument::fromJson(out, &parseErr);
    EXPECT_EQ(parseErr.error, QJsonParseError::NoError);
}

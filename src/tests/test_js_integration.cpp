#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include "stdiolink/platform/platform_utils.h"

namespace {

QString servicePath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "stdiolink_service");
}

QString calculatorDriverPath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "stdio.drv.calculator");
}

QString escapeJsString(const QString& s) {
    QString out = s;
    out.replace("\\", "/");
    out.replace("'", "\\'");
    return out;
}

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QString stdoutText;
    QString stderrText;
};

RunResult runServiceDir(const QString& dirPath,
                        const QStringList& extraArgs = {},
                        int timeoutMs = 30000) {
    QProcess proc;
    QStringList args;
    args << dirPath;
    args << extraArgs;
    proc.start(servicePath(), args);
    RunResult result;
    result.finished = proc.waitForFinished(timeoutMs);
    if (!result.finished) {
        proc.kill();
        proc.waitForFinished(3000);
    }
    result.exitCode = proc.exitCode();
    result.stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
    result.stderrText = QString::fromUtf8(proc.readAllStandardError());
    return result;
}

} // namespace

class JsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(QFileInfo::exists(servicePath()));
        ASSERT_TRUE(QFileInfo::exists(calculatorDriverPath()));
    }

    QString createServiceDir(const QString& jsCode,
                             const QByteArray& schema = "{}") {
        static int counter = 0;
        QString dirPath = m_tmpDir.path() + "/svc_" + QString::number(counter++);
        QDir().mkpath(dirPath);

        QFile mf(dirPath + "/manifest.json");
        EXPECT_TRUE(mf.open(QIODevice::WriteOnly));
        mf.write(R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})");
        mf.close();

        QFile sf(dirPath + "/config.schema.json");
        EXPECT_TRUE(sf.open(QIODevice::WriteOnly));
        sf.write(schema);
        sf.close();

        QFile jf(dirPath + "/index.js");
        EXPECT_TRUE(jf.open(QIODevice::WriteOnly | QIODevice::Text));
        jf.write(jsCode.toUtf8());
        jf.close();

        return dirPath;
    }

    QTemporaryDir m_tmpDir;
};

TEST_F(JsIntegrationTest, BasicDriverUsage) {
    QString dir = createServiceDir(
        QString(
            "import { Driver } from 'stdiolink';\n"
            "const d = new Driver();\n"
            "if (!d.start('%1', ['--profile=keepalive'])) throw new Error('start failed');\n"
            "const t = d.request('add', { a: 10, b: 20 });\n"
            "const m = t.waitNext(5000);\n"
            "if (!m || m.status !== 'done' || !m.data || m.data.result !== 30) throw new Error('bad result');\n"
            "console.log('basic-ok', m.data.result);\n"
            "d.terminate();\n")
            .arg(escapeJsString(calculatorDriverPath())));

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutText.isEmpty());
    EXPECT_TRUE(r.stderrText.contains("basic-ok 30"));
}

TEST_F(JsIntegrationTest, ProxyDriverUsage) {
    QString dir = createServiceDir(
        QString(
            "import { openDriver } from 'stdiolink';\n"
            "(async () => {\n"
            "  const calc = await openDriver('%1');\n"
            "  const r = await calc.add({ a: 5, b: 3 });\n"
            "  if (!r || r.result !== 8) throw new Error('bad proxy result');\n"
            "  console.log('proxy-ok', r.result);\n"
            "  calc.$close();\n"
            "})();\n")
            .arg(escapeJsString(calculatorDriverPath())));

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("proxy-ok 8"));
}

TEST_F(JsIntegrationTest, MultiDriverParallelUsage) {
    QString dir = createServiceDir(
        QString(
            "import { openDriver } from 'stdiolink';\n"
            "(async () => {\n"
            "  const a = await openDriver('%1');\n"
            "  const b = await openDriver('%1');\n"
            "  const rs = await Promise.all([\n"
            "    a.add({ a: 1, b: 2 }),\n"
            "    b.add({ a: 3, b: 7 })\n"
            "  ]);\n"
            "  if (rs[0].result !== 3 || rs[1].result !== 10) throw new Error('parallel mismatch');\n"
            "  console.log('parallel-ok', rs[1].result);\n"
            "  a.$close();\n"
            "  b.$close();\n"
            "})();\n")
            .arg(escapeJsString(calculatorDriverPath())));

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("parallel-ok 10"));
}

TEST_F(JsIntegrationTest, ProcessExecUsage) {
#ifdef Q_OS_WIN
    const QString script =
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'echo', 'hello-m27']);\n"
        "if (r.exitCode !== 0) throw new Error('exec failed');\n"
        "if (!r.stdout.toLowerCase().includes('hello-m27')) throw new Error('stdout mismatch');\n"
        "console.log('exec-ok', r.exitCode);\n";
#else
    const QString script =
        "import { exec } from 'stdiolink';\n"
        "const r = exec('echo', ['hello-m27']);\n"
        "if (r.exitCode !== 0) throw new Error('exec failed');\n"
        "if (!r.stdout.includes('hello-m27')) throw new Error('stdout mismatch');\n"
        "console.log('exec-ok', r.exitCode);\n";
#endif
    QString dir = createServiceDir(script);

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("exec-ok 0"));
}

TEST_F(JsIntegrationTest, DriverStartFailureIsCatchable) {
    QString dir = createServiceDir(
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  try {\n"
        "    await openDriver('__nonexistent_driver__');\n"
        "    throw new Error('expected start failure');\n"
        "  } catch (e) {\n"
        "    console.error('start-fail', String(e));\n"
        "  }\n"
        "})();\n");

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("start-fail"));
}

TEST_F(JsIntegrationTest, ModuleNotFoundFailsProcess) {
    QString dir = createServiceDir(
        "import { missing } from './no_such_file.js';\n"
        "console.log(missing);\n");

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
}

TEST_F(JsIntegrationTest, SyntaxErrorFailsProcess) {
    QString dir = createServiceDir("let = ;\n");

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
}

TEST_F(JsIntegrationTest, ConsoleOutputDoesNotPolluteStdout) {
    QString dir = createServiceDir(
        "console.log('m27-log');\n"
        "console.warn('m27-warn');\n");

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutText.isEmpty());
    EXPECT_TRUE(r.stderrText.contains("m27-log"));
    EXPECT_TRUE(r.stderrText.contains("m27-warn"));
}

TEST_F(JsIntegrationTest, UncaughtExceptionExitsWithError) {
    QString dir = createServiceDir("throw new Error('test uncaught');\n");

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(r.stderrText.contains("test uncaught"));
}

TEST_F(JsIntegrationTest, CrossFileImport) {
    QString dir = createServiceDir(
        "import { add } from './lib.js';\n"
        "const r = add(3, 4);\n"
        "if (r !== 7) throw new Error('cross-file import failed');\n"
        "console.log('cross-file-ok', r);\n");

    // Create lib file in the same service directory
    QFile libFile(dir + "/lib.js");
    ASSERT_TRUE(libFile.open(QIODevice::WriteOnly | QIODevice::Text));
    libFile.write("export function add(a, b) { return a + b; }\n");
    libFile.close();

    const RunResult r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("cross-file-ok 7"));
}

TEST_F(JsIntegrationTest, DumpSchemaOutputsJson) {
    QString dir = createServiceDir(
        "// index.js not executed in dump mode\n",
        R"({"port":{"type":"int","required":true}})");

    const RunResult r = runServiceDir(dir, {"--dump-config-schema"});
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(r.stdoutText.toUtf8(), &err);
    EXPECT_EQ(err.error, QJsonParseError::NoError);
    EXPECT_TRUE(doc.isObject());
    EXPECT_TRUE(doc.object().contains("fields"));
}

TEST_F(JsIntegrationTest, DumpSchemaMalformedFileFails) {
    QString dir = createServiceDir("// unused\n", "{invalid json");

    const RunResult r = runServiceDir(dir, {"--dump-config-schema"});
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 2);
}

TEST_F(JsIntegrationTest, ConfigInjectionViaServiceDir) {
    QByteArray schema = R"({
        "port": { "type": "int", "required": true },
        "name": { "type": "string", "default": "default" }
    })";

    QString dir = createServiceDir(
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "console.log('port:', cfg.port);\n"
        "console.log('name:', cfg.name);\n",
        schema);

    auto r = runServiceDir(dir, {"--config.port=8080"});
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("port: 8080"));
    EXPECT_TRUE(r.stderrText.contains("name: default"));
}

TEST_F(JsIntegrationTest, HelpShowsConfigSchemaHelp) {
    QByteArray schema = R"({
        "port": { "type": "int", "required": true, "description": "Listen port" },
        "debug": { "type": "bool", "default": false, "description": "Debug mode" }
    })";

    QString dir = createServiceDir("// unused\n", schema);

    auto r = runServiceDir(dir, {"--help"});
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("--config.port"));
    EXPECT_TRUE(r.stderrText.contains("Listen port"));
    EXPECT_TRUE(r.stderrText.contains("--config.debug"));
}

TEST_F(JsIntegrationTest, UnknownFieldTypeFailsWithExit2) {
    QByteArray schema = R"({"port": {"type": "integr"}})";

    QString dir = createServiceDir("// unused\n", schema);

    auto r = runServiceDir(dir);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_TRUE(r.stderrText.contains("unknown field type"));
}


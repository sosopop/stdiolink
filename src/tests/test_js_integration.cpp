#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

QString servicePath() {
    QString path = QCoreApplication::applicationDirPath() + "/stdiolink_service";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return QDir::fromNativeSeparators(path);
}

QString calculatorDriverPath() {
    QString path = QCoreApplication::applicationDirPath() + "/calculator_driver";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return QDir::fromNativeSeparators(path);
}

QString escapeJsString(const QString& s) {
    QString out = s;
    out.replace("\\", "/");
    out.replace("'", "\\'");
    return out;
}

QString writeScript(const QTemporaryDir& dir, const QString& name, const QString& content) {
    const QString path = dir.path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&f);
    out << content;
    out.flush();
    return path;
}

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QString stdoutText;
    QString stderrText;
};

RunResult runServiceScript(const QString& scriptPath, int timeoutMs = 30000) {
    QProcess proc;
    proc.start(servicePath(), {scriptPath});
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

    QTemporaryDir m_tmpDir;
};

TEST_F(JsIntegrationTest, BasicDriverUsage) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "basic_usage.js",
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
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutText.isEmpty());
    EXPECT_TRUE(r.stderrText.contains("basic-ok 30"));
}

TEST_F(JsIntegrationTest, ProxyDriverUsage) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "proxy_usage.js",
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
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("proxy-ok 8"));
}

TEST_F(JsIntegrationTest, MultiDriverParallelUsage) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "multi_driver.js",
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
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
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
    const QString scriptPath = writeScript(m_tmpDir, "process_exec.js", script);
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("exec-ok 0"));
}

TEST_F(JsIntegrationTest, DriverStartFailureIsCatchable) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "driver_start_fail.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  try {\n"
        "    await openDriver('__nonexistent_driver__');\n"
        "    throw new Error('expected start failure');\n"
        "  } catch (e) {\n"
        "    console.error('start-fail', String(e));\n"
        "  }\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("start-fail"));
}

TEST_F(JsIntegrationTest, ModuleNotFoundFailsProcess) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "module_not_found.js",
        "import { missing } from './no_such_file.js';\n"
        "console.log(missing);\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
}

TEST_F(JsIntegrationTest, SyntaxErrorFailsProcess) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "syntax_error.js",
        "let = ;\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
}

TEST_F(JsIntegrationTest, ConsoleOutputDoesNotPolluteStdout) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "console_output.js",
        "console.log('m27-log');\n"
        "console.warn('m27-warn');\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutText.isEmpty());
    EXPECT_TRUE(r.stderrText.contains("m27-log"));
    EXPECT_TRUE(r.stderrText.contains("m27-warn"));
}

TEST_F(JsIntegrationTest, UncaughtExceptionExitsWithError) {
    const QString scriptPath = writeScript(
        m_tmpDir,
        "uncaught.js",
        "throw new Error('test uncaught');\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(r.stderrText.contains("test uncaught"));
}

TEST_F(JsIntegrationTest, CrossFileImport) {
    // Create lib file
    QFile libFile(m_tmpDir.path() + "/lib.js");
    ASSERT_TRUE(libFile.open(QIODevice::WriteOnly | QIODevice::Text));
    libFile.write("export function add(a, b) { return a + b; }\n");
    libFile.close();

    // Create main file that imports lib
    const QString scriptPath = writeScript(
        m_tmpDir,
        "main.js",
        "import { add } from './lib.js';\n"
        "const r = add(3, 4);\n"
        "if (r !== 7) throw new Error('cross-file import failed');\n"
        "console.log('cross-file-ok', r);\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    const RunResult r = runServiceScript(scriptPath);
    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrText.contains("cross-file-ok 7"));
}

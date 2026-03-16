#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include "stdiolink/platform/platform_utils.h"

namespace {

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QString stdoutStr;
    QString stderrStr;
};

QString findRepoRootPath() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(dir.absoluteFilePath("src/data_root/services"))
            && QFileInfo::exists(dir.absoluteFilePath("doc/milestone"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString runtimeDataRootPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(appDir).absoluteFilePath("../data_root"),
        QDir(appDir).absoluteFilePath("data_root"),
        QDir(appDir).absoluteFilePath("../runtime_debug/data_root"),
        QDir(appDir).absoluteFilePath("../runtime_release/data_root"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate + "/services")) {
            return candidate;
        }
    }
    return {};
}

QString serviceExecutablePath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        stdiolink::PlatformUtils::executablePath(appDir, "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_release/bin"), "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_debug/bin"), "stdiolink_service"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return candidates.front();
}

QString stubPath(const QString& stubName) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        stdiolink::PlatformUtils::executablePath(appDir, stubName),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_release/bin"), stubName),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_debug/bin"), stubName),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return candidates.front();
}

QString execRunnerServiceDirPath() {
    const QString runtimeDataRoot = runtimeDataRootPath();
    if (!runtimeDataRoot.isEmpty()) {
        const QString runtimeServiceDir =
            QDir(runtimeDataRoot).absoluteFilePath("services/exec_runner");
        if (QFileInfo::exists(runtimeServiceDir + "/index.js")) {
            return runtimeServiceDir;
        }
    }

    const QString repoRoot = findRepoRootPath();
    if (repoRoot.isEmpty()) {
        return {};
    }
    return QDir(repoRoot).absoluteFilePath("src/data_root/services/exec_runner");
}

QProcessEnvironment childEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList extraDirs{
        appDir,
        QDir(appDir).absoluteFilePath("../runtime_release/bin"),
        QDir(appDir).absoluteFilePath("../runtime_debug/bin"),
    };
    QStringList existingDirs;
    for (const QString& dir : extraDirs) {
        if (QFileInfo::exists(dir)) {
            existingDirs.append(dir);
        }
    }
    const QString oldPath = env.value("PATH");
    const QString extraPath = existingDirs.join(QDir::listSeparator());
    env.insert("PATH", oldPath.isEmpty() ? extraPath : (extraPath + QDir::listSeparator() + oldPath));
    return env;
}

} // namespace

class ExecRunnerServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_serviceExe = serviceExecutablePath();
        ASSERT_TRUE(QFileInfo::exists(m_serviceExe))
            << "stdiolink_service not found: " << qPrintable(m_serviceExe);

        m_serviceDir = execRunnerServiceDirPath();
        ASSERT_FALSE(m_serviceDir.isEmpty()) << "exec_runner service dir not found";
        ASSERT_TRUE(QFileInfo::exists(m_serviceDir + "/index.js"))
            << "index.js not found in " << qPrintable(m_serviceDir);
    }

    RunResult runService(const QStringList& extraArgs, int timeoutMs = 20000) const {
        QProcess proc;
        proc.setProcessEnvironment(childEnv());

        QStringList args;
        args << m_serviceDir;
        args.append(extraArgs);

        proc.start(m_serviceExe, args);

        RunResult result;
        if (!proc.waitForStarted(5000)) {
            result.stderrStr = "failed to start stdiolink_service";
            return result;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (proc.waitForFinished(20)) {
                result.finished = true;
                break;
            }
        }
        if (!result.finished) {
            proc.kill();
            proc.waitForFinished(3000);
        }

        result.exitCode = proc.exitCode();
        result.stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
        result.stderrStr = QString::fromUtf8(proc.readAllStandardError());
        return result;
    }

    RunResult dumpSchema() const {
        return runService({"--dump-config-schema"}, 10000);
    }

    QString m_serviceExe;
    QString m_serviceDir;
};

// T01 — stdout 成功路径
TEST_F(ExecRunnerServiceTest, T01_SpawnSuccessStdout) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--text=hello_exec_runner",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("hello_exec_runner")
                || r.stderrStr.contains("hello_exec_runner"))
        << "expected 'hello_exec_runner' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T02 — stderr 实时输出路径
TEST_F(ExecRunnerServiceTest, T02_SpawnSuccessStderr) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stderr",
        "--config.args[1]=--text=stderr_line",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("stderr_line")
                || r.stderrStr.contains("stderr_line"))
        << "expected 'stderr_line' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T03 — program 缺失
TEST_F(ExecRunnerServiceTest, T03_RejectsMissingProgram) {
    auto r = runService({});
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_NE(r.exitCode, 0)
        << "expected non-zero exit code for missing program\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stderrStr.contains("config validation failed")
                || r.stderrStr.contains("program"))
        << "expected validation error in stderr\nstderr:\n" << qPrintable(r.stderrStr);
}

// T04 — 程序不存在
TEST_F(ExecRunnerServiceTest, T04_NonexistentProgramFails) {
    auto r = runService({
        "--config.program=nonexistent_exec_runner_binary_xyz",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_NE(r.exitCode, 0)
        << "expected non-zero exit for nonexistent program\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("crash") || r.stderrStr.contains("crash")
                || r.stdoutStr.contains("-1") || r.stderrStr.contains("-1")
                || r.stderrStr.contains("process crashed"))
        << "expected crash/error indication in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T05 — 非零退出码判失败
TEST_F(ExecRunnerServiceTest, T05_NonZeroExitCodeFails) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--exit-code=7",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_NE(r.exitCode, 0)
        << "expected non-zero exit code\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("process exited with code 7")
                || r.stderrStr.contains("process exited with code 7"))
        << "expected 'process exited with code 7' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T06 — 非零退出码白名单
TEST_F(ExecRunnerServiceTest, T06_CustomSuccessExitCodes) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--exit-code=7",
        "--config.success_exit_codes[0]=0",
        "--config.success_exit_codes[1]=7",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "expected exit code 0 with custom success_exit_codes\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T07 — args 数组透传
TEST_F(ExecRunnerServiceTest, T07_ArgsArrayPassthrough) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--text=arg_ok",
    });
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("arg_ok") || r.stderrStr.contains("arg_ok"))
        << "expected 'arg_ok' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T08 — env 对象透传
TEST_F(ExecRunnerServiceTest, T08_EnvPassthrough) {
#ifdef Q_OS_WIN
    auto r = runService({
        "--config.program=cmd.exe",
        "--config.args[0]=/c",
        "--config.args[1]=echo %MY_VAR%",
        "--config.env.MY_VAR=test123",
    });
#else
    auto r = runService({
        "--config.program=/bin/sh",
        "--config.args[0]=-c",
        "--config.args[1]=printf '%s' \"$MY_VAR\"",
        "--config.env.MY_VAR=test123",
    });
#endif
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("test123") || r.stderrStr.contains("test123"))
        << "expected 'test123' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T08B — env 非字符串值会被稳定转成字符串
TEST_F(ExecRunnerServiceTest, T08B_EnvValuesAreStringified) {
#ifdef Q_OS_WIN
    auto r = runService({
        "--config.program=cmd.exe",
        "--config.args[0]=/c",
        "--config.args[1]=echo %MY_NUM%",
        "--config.env.MY_NUM=42",
    });
#else
    auto r = runService({
        "--config.program=/bin/sh",
        "--config.args[0]=-c",
        "--config.args[1]=printf '%s' \"$MY_NUM\"",
        "--config.env.MY_NUM=42",
    });
#endif
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
    EXPECT_TRUE(r.stdoutStr.contains("42") || r.stderrStr.contains("42"))
        << "expected '42' in output\nstdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);
}

// T09 — schema 导出
TEST_F(ExecRunnerServiceTest, T09_DumpConfigSchemaDoesNotExposeLegacyFields) {
    auto r = dumpSchema();
    EXPECT_TRUE(r.finished) << "process did not finish in time";
    EXPECT_EQ(r.exitCode, 0)
        << "stdout:\n" << qPrintable(r.stdoutStr)
        << "\nstderr:\n" << qPrintable(r.stderrStr);

    // Verify expected fields exist
    EXPECT_TRUE(r.stdoutStr.contains("\"program\""))
        << "schema should contain 'program' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"args\""))
        << "schema should contain 'args' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"env\""))
        << "schema should contain 'env' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"success_exit_codes\""))
        << "schema should contain 'success_exit_codes' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"cwd\""))
        << "schema should contain 'cwd' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"log_stdout\""))
        << "schema should contain 'log_stdout' field";
    EXPECT_TRUE(r.stdoutStr.contains("\"log_stderr\""))
        << "schema should contain 'log_stderr' field";

    // Verify legacy fields are absent
    EXPECT_FALSE(r.stdoutStr.contains("\"mode\""))
        << "schema should NOT contain legacy 'mode' field";
    EXPECT_FALSE(r.stdoutStr.contains("\"command\""))
        << "schema should NOT contain legacy 'command' field";
    EXPECT_FALSE(r.stdoutStr.contains("\"timeout_ms\""))
        << "schema should NOT contain legacy 'timeout_ms' field";
    EXPECT_FALSE(r.stdoutStr.contains("\"input\""))
        << "schema should NOT contain legacy 'input' field";
}

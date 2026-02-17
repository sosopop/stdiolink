#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QProcess>
#include <QThread>

#include "stdiolink/guard/process_tree_guard.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace stdiolink;

namespace {

QString parentStubPath() {
    return QCoreApplication::applicationDirPath() + "/test_tree_guard_parent_stub"
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;
}

QString checkStubPath() {
    return QCoreApplication::applicationDirPath() + "/test_tree_guard_check_stub"
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;
}

QString guardStubPath() {
    return QCoreApplication::applicationDirPath() + "/test_guard_stub"
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;
}

} // namespace

// ── Windows 专用测试 ────────────────────────────────────────────────

#ifdef Q_OS_WIN

// T01 — Windows Job Object 创建成功
TEST(ProcessTreeGuard, T01_JobObjectCreated) {
    ProcessTreeGuard guard;
    EXPECT_TRUE(guard.isValid());
}

// T03 — Windows adoptProcess 成功
TEST(ProcessTreeGuard, T03_AdoptProcessSuccess) {
    ProcessTreeGuard guard;
    ASSERT_TRUE(guard.isValid());

    QProcess proc;
    proc.start(guardStubPath(), {"--guard=stdiolink_guard_dummy_t03"});
    ASSERT_TRUE(proc.waitForStarted(5000));

    EXPECT_TRUE(guard.adoptProcess(&proc));

    proc.kill();
    proc.waitForFinished(3000);
}

// T04 — Windows adoptProcess 在 isValid()==false 时跳过
TEST(ProcessTreeGuard, T04_AdoptProcessInvalidHandle) {
    ProcessTreeGuard guard;
    guard.invalidateForTesting();
    EXPECT_FALSE(guard.isValid());

    QProcess proc;
    proc.start(guardStubPath(), {"--guard=stdiolink_guard_dummy_t04"});
    ASSERT_TRUE(proc.waitForStarted(5000));

    EXPECT_FALSE(guard.adoptProcess(&proc));

    proc.kill();
    proc.waitForFinished(3000);
}

// T04_b — Windows adoptProcess 在 handle 被销毁后返回 false
TEST(ProcessTreeGuard, T04b_AdoptProcessAfterInvalidate) {
    ProcessTreeGuard guard;
    ASSERT_TRUE(guard.isValid());

    QProcess proc;
    proc.start(guardStubPath(), {"--guard=stdiolink_guard_dummy_t04b"});
    ASSERT_TRUE(proc.waitForStarted(5000));

    // Invalidate after construction to simulate failure path
    guard.invalidateForTesting();

    // adoptProcess should return false and emit qWarning (not asserted here
    // since Qt6::Test is not linked; warning is verified by manual log inspection)
    EXPECT_FALSE(guard.adoptProcess(&proc));

    proc.kill();
    proc.waitForFinished(3000);
}

// T05 — Windows adoptProcess 进程未启动
TEST(ProcessTreeGuard, T05_AdoptProcessNotStarted) {
    ProcessTreeGuard guard;
    ASSERT_TRUE(guard.isValid());

    QProcess proc;
    // proc not started — processId() == 0
    EXPECT_FALSE(guard.adoptProcess(&proc));
}

// T06 — Windows prepareProcess 空实现
TEST(ProcessTreeGuard, T06_PrepareProcessNoop) {
    ProcessTreeGuard guard;

    QProcess proc;
    guard.prepareProcess(&proc);
    proc.start(checkStubPath());
    ASSERT_TRUE(proc.waitForStarted(5000));
    ASSERT_TRUE(proc.waitForFinished(5000));
    EXPECT_EQ(proc.exitCode(), 0);
}

// T08 — Windows 父进程退出后子进程被 Job Object 终止
TEST(ProcessTreeGuard, T08_ParentKilledChildTerminatedByJob) {
    QProcess parentProc;
    parentProc.start(parentStubPath());
    ASSERT_TRUE(parentProc.waitForStarted(5000));

    // 读取子进程 PID
    ASSERT_TRUE(parentProc.waitForReadyRead(5000));
    const QByteArray line = parentProc.readLine().trimmed();
    const qint64 childPid = line.toLongLong();
    ASSERT_GT(childPid, 0) << "Failed to read child PID, got: " << line.toStdString();

    // 获取子进程句柄（在杀父进程前，避免 PID 复用）
    HANDLE hChild = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(childPid));
    ASSERT_NE(hChild, nullptr) << "OpenProcess failed for child pid " << childPid;

    // 杀死父进程 → Job Object 关闭 → 子进程被 OS 终止
    parentProc.kill();
    parentProc.waitForFinished(3000);

    // 等待子进程退出
    DWORD waitResult = WaitForSingleObject(hChild, 5000);
    CloseHandle(hChild);
    EXPECT_EQ(waitResult, WAIT_OBJECT_0) << "Child process did not exit after parent was killed";
}

// T11 — Windows 多子进程均受同一 Job Object 守护
TEST(ProcessTreeGuard, T11_MultipleChildrenInJob) {
    ProcessTreeGuard guard;
    ASSERT_TRUE(guard.isValid());

    QProcess proc1, proc2;
    proc1.start(guardStubPath(), {"--guard=stdiolink_guard_dummy_t11a"});
    proc2.start(guardStubPath(), {"--guard=stdiolink_guard_dummy_t11b"});
    ASSERT_TRUE(proc1.waitForStarted(5000));
    ASSERT_TRUE(proc2.waitForStarted(5000));

    EXPECT_TRUE(guard.adoptProcess(&proc1));
    EXPECT_TRUE(guard.adoptProcess(&proc2));

    proc1.kill();
    proc1.waitForFinished(3000);
    proc2.kill();
    proc2.waitForFinished(3000);
}

// T12 — 正常退出时 Job 关闭无副作用
TEST(ProcessTreeGuard, T12_NormalExitJobCloseSafe) {
    {
        ProcessTreeGuard guard;
        ASSERT_TRUE(guard.isValid());

        QProcess proc;
        proc.start(checkStubPath());
        ASSERT_TRUE(proc.waitForStarted(5000));
        guard.adoptProcess(&proc);

        // 子进程正常退出
        ASSERT_TRUE(proc.waitForFinished(5000));
        EXPECT_EQ(proc.exitCode(), 0);
    }
    // ProcessTreeGuard 析构 — 不应崩溃
    SUCCEED();
}

#endif // Q_OS_WIN

// ── Linux 专用测试 ──────────────────────────────────────────────────

#ifdef Q_OS_LINUX

// T02 — Linux PDEATHSIG 设置验证
TEST(ProcessTreeGuard, T02_PdeathsigSet) {
    ProcessTreeGuard guard;

    QProcess proc;
    guard.prepareProcess(&proc);
    proc.start(checkStubPath());
    ASSERT_TRUE(proc.waitForStarted(5000));
    ASSERT_TRUE(proc.waitForFinished(5000));
    EXPECT_EQ(proc.exitCode(), 0);

    const QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    // 期望输出 "GUARD_STATUS:9" (SIGKILL == 9)
    EXPECT_EQ(output, "GUARD_STATUS:9")
        << "Expected PDEATHSIG=SIGKILL(9), got: " << output.toStdString();
}

// T07 — Linux adoptProcess 空实现
TEST(ProcessTreeGuard, T07_AdoptProcessNoop) {
    ProcessTreeGuard guard;

    QProcess proc;
    proc.start(checkStubPath());
    ASSERT_TRUE(proc.waitForStarted(5000));

    EXPECT_TRUE(guard.adoptProcess(&proc));

    proc.waitForFinished(5000);
}

// T09 — Linux 父进程退出后子进程被 PDEATHSIG 终止
TEST(ProcessTreeGuard, T09_ParentKilledChildTerminatedByPdeathsig) {
    // 成为 subreaper 以接管孙进程
    ASSERT_EQ(prctl(PR_SET_CHILD_SUBREAPER, 1), 0);

    QProcess parentProc;
    parentProc.start(parentStubPath());
    ASSERT_TRUE(parentProc.waitForStarted(5000));

    // 读取子进程 PID
    ASSERT_TRUE(parentProc.waitForReadyRead(5000));
    const QByteArray line = parentProc.readLine().trimmed();
    const pid_t childPid = static_cast<pid_t>(line.toLongLong());
    ASSERT_GT(childPid, 0) << "Failed to read child PID, got: " << line.toStdString();

    // 给子进程时间连接 guard server
    QThread::msleep(500);

    // 杀死父进程 → PDEATHSIG 触发 → 子进程收到 SIGKILL
    parentProc.kill();
    parentProc.waitForFinished(3000);

    // 等待孙进程（已被 reparent 到本进程）
    int status = 0;
    // 设置 alarm 防止永久阻塞
    alarm(5);
    const pid_t waited = waitpid(childPid, &status, 0);
    alarm(0);

    ASSERT_EQ(waited, childPid) << "waitpid failed, errno=" << errno;
    EXPECT_TRUE(WIFSIGNALED(status)) << "Child was not killed by signal";
    if (WIFSIGNALED(status)) {
        EXPECT_EQ(WTERMSIG(status), SIGKILL)
            << "Expected SIGKILL, got signal " << WTERMSIG(status);
    }

    // 恢复 subreaper 状态
    prctl(PR_SET_CHILD_SUBREAPER, 0);
}

#endif // Q_OS_LINUX

// ── 跨平台测试 ─────────────────────────────────────────────────────

// T10 — prepareProcess + adoptProcess 联合流程验证（子进程报告自身守护状态）
TEST(ProcessTreeGuard, T10_GuardStatusReported) {
    ProcessTreeGuard guard;

    QProcess proc;
    guard.prepareProcess(&proc);
    proc.start(checkStubPath());
    ASSERT_TRUE(proc.waitForStarted(5000));
    guard.adoptProcess(&proc);

    ASSERT_TRUE(proc.waitForFinished(5000));
    EXPECT_EQ(proc.exitCode(), 0);

    const QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

#ifdef Q_OS_WIN
    // Windows: IsProcessInJob 应返回 1
    EXPECT_EQ(output, "GUARD_STATUS:1")
        << "Expected child in job, got: " << output.toStdString();
#elif defined(Q_OS_LINUX)
    // Linux: PDEATHSIG 应为 9 (SIGKILL)
    EXPECT_EQ(output, "GUARD_STATUS:9")
        << "Expected PDEATHSIG=SIGKILL(9), got: " << output.toStdString();
#else
    // 其他平台: 无守护机制
    EXPECT_EQ(output, "GUARD_STATUS:0");
#endif
}

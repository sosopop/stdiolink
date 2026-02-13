#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QProcess>
#include <QThread>

#include "stdiolink_server/manager/process_monitor.h"

using namespace stdiolink_server;

// ── ProcessMonitor unit tests ───────────────────────────────────────

TEST(ProcessMonitorTest, GetCurrentProcessInfo) {
    ProcessMonitor monitor;
    auto info = monitor.getProcessInfo(QCoreApplication::applicationPid());

    EXPECT_EQ(info.pid, QCoreApplication::applicationPid());
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_FALSE(info.name.isEmpty()) << "Process name should not be empty";
    EXPECT_TRUE(info.isValid());
#endif
}

TEST(ProcessMonitorTest, CurrentProcessRssPositive) {
    ProcessMonitor monitor;
    auto info = monitor.getProcessInfo(QCoreApplication::applicationPid());

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_GT(info.memoryRssBytes, 0) << "RSS should be > 0 for current process";
#endif
}

TEST(ProcessMonitorTest, CurrentProcessThreadCountAtLeastOne) {
    ProcessMonitor monitor;
    auto info = monitor.getProcessInfo(QCoreApplication::applicationPid());

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_GE(info.threadCount, 1) << "Thread count should be >= 1";
#endif
}

TEST(ProcessMonitorTest, NonExistentPidReturnsInvalid) {
    ProcessMonitor monitor;
    // PID 999999999 is extremely unlikely to exist
    auto info = monitor.getProcessInfo(999999999);
    EXPECT_FALSE(info.isValid());
}

TEST(ProcessMonitorTest, GetProcessTreeForCurrentProcess) {
    ProcessMonitor monitor;
    auto tree = monitor.getProcessTree(QCoreApplication::applicationPid());

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_EQ(tree.info.pid, QCoreApplication::applicationPid());
    EXPECT_TRUE(tree.info.isValid());
#endif
}

TEST(ProcessMonitorTest, ProcessTreeIncludesChildProcess) {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_LINUX)
    GTEST_SKIP() << "ProcessMonitor not implemented on this platform";
#endif

    // Start a child process (sleep)
    QProcess child;
    child.start("sleep", {"10"});
    ASSERT_TRUE(child.waitForStarted(3000)) << "Failed to start child process";

    ProcessMonitor monitor;
    auto tree = monitor.getProcessTree(QCoreApplication::applicationPid());

    // Find the child in the tree
    bool foundChild = false;
    for (const auto& node : tree.children) {
        if (node.info.pid == child.processId()) {
            foundChild = true;
            break;
        }
    }
    EXPECT_TRUE(foundChild) << "Child process should appear in process tree";

    child.terminate();
    child.waitForFinished(3000);
}

TEST(ProcessMonitorTest, CleanupSamplesRemovesExitedPids) {
    ProcessMonitor monitor;

    // First call stores a CPU sample
    auto info1 = monitor.getProcessInfo(QCoreApplication::applicationPid());

    // Start and immediately kill a child to get a stale PID
    QProcess child;
    child.start("sleep", {"10"});
    ASSERT_TRUE(child.waitForStarted(3000));
    qint64 childPid = child.processId();

    // Sample the child
    monitor.getProcessInfo(childPid);

    // Kill the child
    child.kill();
    child.waitForFinished(3000);

    // Now get the tree — this should clean up the dead child's sample
    auto tree = monitor.getProcessTree(QCoreApplication::applicationPid());

    // We can't directly inspect m_cpuSamples, but the test verifies
    // that getProcessTree doesn't crash after cleanup
    EXPECT_TRUE(tree.info.isValid());
}

TEST(ProcessMonitorTest, CpuPercentFirstSampleReturnsZero) {
    ProcessMonitor monitor;
    auto info = monitor.getProcessInfo(QCoreApplication::applicationPid());

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_DOUBLE_EQ(info.cpuPercent, 0.0) << "First CPU sample should return 0";
#endif
}

TEST(ProcessMonitorTest, CpuPercentSecondSampleNonNegative) {
    ProcessMonitor monitor;

    // First sample
    monitor.getProcessInfo(QCoreApplication::applicationPid());

    // Do some work to burn CPU
    volatile int sum = 0;
    for (int i = 0; i < 1000000; ++i)
        sum += i;
    (void)sum;

    // Brief pause to get a measurable wall-time delta
    QThread::msleep(50);

    // Second sample
    auto info = monitor.getProcessInfo(QCoreApplication::applicationPid());

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_GE(info.cpuPercent, 0.0) << "CPU% should be >= 0 on second sample";
#endif
}

TEST(ProcessMonitorTest, SummarizeTree) {
    ProcessTreeNode root;
    root.info.pid = 1;
    root.info.name = "root";
    root.info.cpuPercent = 5.0;
    root.info.memoryRssBytes = 1000;
    root.info.threadCount = 2;

    ProcessTreeNode child;
    child.info.pid = 2;
    child.info.name = "child";
    child.info.cpuPercent = 3.0;
    child.info.memoryRssBytes = 500;
    child.info.threadCount = 1;
    root.children.append(child);

    auto summary = ProcessMonitor::summarize(root);
    EXPECT_EQ(summary.totalProcesses, 2);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 8.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 1500);
    EXPECT_EQ(summary.totalThreads, 3);
}

TEST(ProcessMonitorTest, GetProcessFamilyFlat) {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_LINUX)
    GTEST_SKIP() << "ProcessMonitor not implemented on this platform";
#endif

    QProcess child;
    child.start("sleep", {"10"});
    ASSERT_TRUE(child.waitForStarted(3000));

    ProcessMonitor monitor;
    auto family = monitor.getProcessFamily(QCoreApplication::applicationPid(), true);

    EXPECT_GE(family.size(), 2) << "Family should include at least root + child";

    // Root should be first
    EXPECT_EQ(family[0].pid, QCoreApplication::applicationPid());

    // Child should be in the list
    bool foundChild = false;
    for (const auto& p : family) {
        if (p.pid == child.processId()) {
            foundChild = true;
            break;
        }
    }
    EXPECT_TRUE(foundChild);

    child.terminate();
    child.waitForFinished(3000);
}

TEST(ProcessMonitorTest, GetProcessFamilyWithoutChildren) {
#if !defined(Q_OS_MACOS) || !defined(Q_OS_LINUX)
    // This test works on all platforms since includeChildren=false
    // just returns the root process
#endif

    ProcessMonitor monitor;
    auto family = monitor.getProcessFamily(QCoreApplication::applicationPid(), false);

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_EQ(family.size(), 1) << "Without children, should return only root";
    EXPECT_EQ(family[0].pid, QCoreApplication::applicationPid());
#endif
}

TEST(ProcessMonitorTest, SummarizeFlatList) {
    QVector<ProcessInfo> procs;

    ProcessInfo p1;
    p1.pid = 1;
    p1.name = "a";
    p1.cpuPercent = 10.0;
    p1.memoryRssBytes = 2000;
    p1.threadCount = 4;
    procs.append(p1);

    ProcessInfo p2;
    p2.pid = 2;
    p2.name = "b";
    p2.cpuPercent = 5.0;
    p2.memoryRssBytes = 1000;
    p2.threadCount = 2;
    procs.append(p2);

    auto summary = ProcessMonitor::summarize(procs);
    EXPECT_EQ(summary.totalProcesses, 2);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 15.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 3000);
    EXPECT_EQ(summary.totalThreads, 6);
}

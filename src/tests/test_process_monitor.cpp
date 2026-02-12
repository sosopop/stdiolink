#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QProcess>
#include <QThread>

#include "stdiolink_server/manager/process_monitor.h"

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace stdiolink_server;

// ---------------------------------------------------------------------------
// summarize (pure logic, works on all platforms)
// ---------------------------------------------------------------------------

TEST(ProcessMonitorTest, SummarizeFromTree) {
    ProcessTreeNode root;
    root.info.pid = 1;
    root.info.cpuPercent = 5.0;
    root.info.memoryRssBytes = 1000;
    root.info.threadCount = 2;

    ProcessTreeNode child;
    child.info.pid = 2;
    child.info.cpuPercent = 3.0;
    child.info.memoryRssBytes = 500;
    child.info.threadCount = 1;
    root.children.append(child);

    const ProcessTreeSummary summary = ProcessMonitor::summarize(root);
    EXPECT_EQ(summary.totalProcesses, 2);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 8.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 1500);
    EXPECT_EQ(summary.totalThreads, 3);
}

TEST(ProcessMonitorTest, SummarizeFromFlatList) {
    QVector<ProcessInfo> procs;
    ProcessInfo a;
    a.pid = 10;
    a.cpuPercent = 1.5;
    a.memoryRssBytes = 2048;
    a.threadCount = 4;
    procs.append(a);

    ProcessInfo b;
    b.pid = 11;
    b.cpuPercent = 2.5;
    b.memoryRssBytes = 4096;
    b.threadCount = 2;
    procs.append(b);

    const ProcessTreeSummary summary = ProcessMonitor::summarize(procs);
    EXPECT_EQ(summary.totalProcesses, 2);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 4.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 6144);
    EXPECT_EQ(summary.totalThreads, 6);
}

TEST(ProcessMonitorTest, SummarizeEmptyList) {
    const ProcessTreeSummary summary = ProcessMonitor::summarize(QVector<ProcessInfo>{});
    EXPECT_EQ(summary.totalProcesses, 0);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 0.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 0);
    EXPECT_EQ(summary.totalThreads, 0);
}

TEST(ProcessMonitorTest, SummarizeDeepTree) {
    ProcessTreeNode root;
    root.info.pid = 1;
    root.info.cpuPercent = 1.0;
    root.info.memoryRssBytes = 100;
    root.info.threadCount = 1;

    ProcessTreeNode child;
    child.info.pid = 2;
    child.info.cpuPercent = 2.0;
    child.info.memoryRssBytes = 200;
    child.info.threadCount = 1;

    ProcessTreeNode grandchild;
    grandchild.info.pid = 3;
    grandchild.info.cpuPercent = 3.0;
    grandchild.info.memoryRssBytes = 300;
    grandchild.info.threadCount = 1;

    child.children.append(grandchild);
    root.children.append(child);

    const ProcessTreeSummary summary = ProcessMonitor::summarize(root);
    EXPECT_EQ(summary.totalProcesses, 3);
    EXPECT_DOUBLE_EQ(summary.totalCpuPercent, 6.0);
    EXPECT_EQ(summary.totalMemoryRssBytes, 600);
    EXPECT_EQ(summary.totalThreads, 3);
}

// ---------------------------------------------------------------------------
// ProcessInfo / ProcessTreeNode / ProcessTreeSummary toJson
// ---------------------------------------------------------------------------

TEST(ProcessMonitorTest, ProcessInfoToJson) {
    ProcessInfo info;
    info.pid = 42;
    info.parentPid = 1;
    info.name = "test_proc";
    info.status = "running";
    info.memoryRssBytes = 1024;
    info.threadCount = 3;

    const QJsonObject obj = info.toJson();
    EXPECT_EQ(obj.value("pid").toInteger(), 42);
    EXPECT_EQ(obj.value("parentPid").toInteger(), 1);
    EXPECT_EQ(obj.value("name").toString(), "test_proc");
    EXPECT_EQ(obj.value("status").toString(), "running");
    EXPECT_EQ(obj.value("memoryRssBytes").toInteger(), 1024);
    EXPECT_EQ(obj.value("threadCount").toInt(), 3);
}

TEST(ProcessMonitorTest, ProcessTreeSummaryToJson) {
    ProcessTreeSummary summary;
    summary.totalProcesses = 5;
    summary.totalCpuPercent = 12.5;
    summary.totalMemoryRssBytes = 8192;
    summary.totalThreads = 10;

    const QJsonObject obj = summary.toJson();
    EXPECT_EQ(obj.value("totalProcesses").toInt(), 5);
    EXPECT_DOUBLE_EQ(obj.value("totalCpuPercent").toDouble(), 12.5);
    EXPECT_EQ(obj.value("totalMemoryRssBytes").toInteger(), 8192);
    EXPECT_EQ(obj.value("totalThreads").toInt(), 10);
}

// ---------------------------------------------------------------------------
// Platform-dependent tests (macOS / Linux)
// ---------------------------------------------------------------------------

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)

TEST(ProcessMonitorTest, GetCurrentProcessInfo) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();
    const ProcessInfo info = monitor.getProcessInfo(myPid);

    EXPECT_EQ(info.pid, myPid);
    EXPECT_FALSE(info.name.isEmpty());
    EXPECT_GT(info.memoryRssBytes, 0);
    EXPECT_GE(info.threadCount, 1);
}

TEST(ProcessMonitorTest, GetNonExistentProcess) {
    ProcessMonitor monitor;
    // PID 999999999 is very unlikely to exist
    const ProcessInfo info = monitor.getProcessInfo(999999999);
    EXPECT_EQ(info.pid, 999999999);
    // On failure, name should be empty or "unknown"
    EXPECT_TRUE(info.name.isEmpty() || info.name == "unknown");
}

TEST(ProcessMonitorTest, GetProcessTree) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();
    const ProcessTreeNode tree = monitor.getProcessTree(myPid);

    EXPECT_EQ(tree.info.pid, myPid);
    EXPECT_FALSE(tree.info.name.isEmpty());
}

TEST(ProcessMonitorTest, CpuPercentFirstSampleIsZero) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();
    const ProcessInfo info = monitor.getProcessInfo(myPid);

    // First sample should return 0
    EXPECT_DOUBLE_EQ(info.cpuPercent, 0.0);
}

TEST(ProcessMonitorTest, CpuPercentSecondSampleNonNegative) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();

    // First sample
    monitor.getProcessInfo(myPid);

    // Do some work to burn CPU
    volatile int sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;
    }
    (void)sum;

    QThread::msleep(50);

    // Second sample
    const ProcessInfo info2 = monitor.getProcessInfo(myPid);
    EXPECT_GE(info2.cpuPercent, 0.0);
}

TEST(ProcessMonitorTest, GetProcessFamilyRootOnly) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();

    const QVector<ProcessInfo> family = monitor.getProcessFamily(myPid, false);
    ASSERT_EQ(family.size(), 1);
    EXPECT_EQ(family[0].pid, myPid);
}

TEST(ProcessMonitorTest, GetProcessFamilyWithChild) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();

    // Start a child process
    QProcess child;
    child.start("sleep", {"10"});
    ASSERT_TRUE(child.waitForStarted(3000));

    const QVector<ProcessInfo> family = monitor.getProcessFamily(myPid, true);

    // Should contain at least ourselves and the child
    EXPECT_GE(family.size(), 2);

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

TEST(ProcessMonitorTest, ProcessTreeIncludesChild) {
    ProcessMonitor monitor;
    const qint64 myPid = QCoreApplication::applicationPid();

    QProcess child;
    child.start("sleep", {"10"});
    ASSERT_TRUE(child.waitForStarted(3000));

    const ProcessTreeNode tree = monitor.getProcessTree(myPid);
    EXPECT_EQ(tree.info.pid, myPid);

    // Find child in tree
    bool foundChild = false;
    for (const auto& c : tree.children) {
        if (c.info.pid == child.processId()) {
            foundChild = true;
            break;
        }
    }
    EXPECT_TRUE(foundChild);

    const ProcessTreeSummary summary = ProcessMonitor::summarize(tree);
    EXPECT_GE(summary.totalProcesses, 2);

    child.terminate();
    child.waitForFinished(3000);
}

#endif // Q_OS_MACOS || Q_OS_LINUX

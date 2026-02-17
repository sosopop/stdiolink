// T08/T09 辅助父进程 stub
// 创建 ProcessTreeGuard + ProcessGuardServer，启动 test_guard_stub 子进程，
// 输出子进程 PID，然后 sleep 等待被杀。
// 当本进程被杀时：
//   Windows: Job Object 关闭 → 子进程被 OS 终止
//   Linux:   子进程收到 PDEATHSIG (SIGKILL)

#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <cstdio>
#include "stdiolink/guard/process_guard_server.h"
#include "stdiolink/guard/process_tree_guard.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QString stubPath = app.applicationDirPath() + "/test_guard_stub"
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;

    // 创建 guard server 让子进程的 ProcessGuardClient 能连接并保持存活
    stdiolink::ProcessGuardServer guardServer;
    if (!guardServer.start()) {
        fprintf(stderr, "Failed to start guard server\n");
        return 2;
    }

    stdiolink::ProcessTreeGuard treeGuard;
    QProcess child;
    treeGuard.prepareProcess(&child);
    child.setProgram(stubPath);
    child.setArguments({"--guard=" + guardServer.guardName()});
    child.start();
    if (!child.waitForStarted(3000)) {
        fprintf(stderr, "Failed to start child\n");
        return 2;
    }
    treeGuard.adoptProcess(&child);

    // 输出子进程 PID 到 stdout，供测试进程读取
    fprintf(stdout, "%lld\n", child.processId());
    fflush(stdout);

    // Sleep 等待被杀
    QThread::sleep(60);
    return 0;
}

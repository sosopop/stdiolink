#pragma once

#include <QProcess>
#include "stdiolink/stdiolink_export.h"

#ifdef Q_OS_LINUX
#include <sys/types.h>
#endif

namespace stdiolink {

/**
 * OS 级进程树守护
 * Windows: Job Object (KILL_ON_JOB_CLOSE)
 * Linux: prctl(PR_SET_PDEATHSIG, SIGKILL)
 *
 * 与 ProcessGuardServer/Client (QLocalSocket) 方案互为双保险。
 */
class STDIOLINK_API ProcessTreeGuard {
public:
    ProcessTreeGuard();
    ~ProcessTreeGuard();

    ProcessTreeGuard(const ProcessTreeGuard&) = delete;
    ProcessTreeGuard& operator=(const ProcessTreeGuard&) = delete;

    /// QProcess::start() 前调用 — Linux 下设置 setChildProcessModifier
    void prepareProcess(QProcess* process);

    /// QProcess::start() 后调用 — Windows 下将子进程加入 Job Object
    bool adoptProcess(QProcess* process);

    /// Job Object 是否有效（Windows），Linux/其他平台始终返回 true
    bool isValid() const;

    /// 测试用：使 Job Object handle 失效，模拟创建失败
    void invalidateForTesting();

private:
#ifdef Q_OS_WIN
    void* m_jobHandle = nullptr;
#endif
#ifdef Q_OS_LINUX
    pid_t m_parentPid = 0;
#endif
};

} // namespace stdiolink

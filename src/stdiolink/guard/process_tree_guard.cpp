#include "process_tree_guard.h"
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace stdiolink {

Q_LOGGING_CATEGORY(lcTreeGuard, "stdiolink.treeguard")

// ── Windows ─────────────────────────────────────────────────────────

#ifdef Q_OS_WIN

ProcessTreeGuard::ProcessTreeGuard() {
    m_jobHandle = CreateJobObjectW(NULL, NULL);
    if (!m_jobHandle) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: CreateJobObject failed, error"
                               << GetLastError();
        return;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(m_jobHandle, JobObjectExtendedLimitInformation,
                                 &info, sizeof(info))) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: SetInformationJobObject failed, error"
                               << GetLastError();
        CloseHandle(m_jobHandle);
        m_jobHandle = nullptr;
    }
}

ProcessTreeGuard::~ProcessTreeGuard() {
    if (m_jobHandle) {
        CloseHandle(m_jobHandle);
        m_jobHandle = nullptr;
    }
}

void ProcessTreeGuard::prepareProcess(QProcess* /*process*/) {
    // Windows: 空实现，不需要 child modifier
}

bool ProcessTreeGuard::adoptProcess(QProcess* process) {
    if (!m_jobHandle) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: adoptProcess skipped, job handle invalid";
        return false;
    }
    if (!process || process->processId() == 0) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: adoptProcess failed, process not started";
        return false;
    }

    const DWORD pid = static_cast<DWORD>(process->processId());
    HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: OpenProcess failed for pid" << pid
                               << ", error" << GetLastError();
        return false;
    }

    const BOOL ok = AssignProcessToJobObject(m_jobHandle, hProc);
    if (!ok) {
        qCWarning(lcTreeGuard) << "ProcessTreeGuard: AssignProcessToJobObject failed for pid"
                               << pid << ", error" << GetLastError();
    }
    CloseHandle(hProc);
    return ok != 0;
}

bool ProcessTreeGuard::isValid() const {
    return m_jobHandle != nullptr;
}

void ProcessTreeGuard::invalidateForTesting() {
    if (m_jobHandle) {
        CloseHandle(m_jobHandle);
        m_jobHandle = nullptr;
    }
}

// ── Linux ───────────────────────────────────────────────────────────

#elif defined(Q_OS_LINUX)

ProcessTreeGuard::ProcessTreeGuard()
    : m_parentPid(getpid()) {
}

ProcessTreeGuard::~ProcessTreeGuard() = default;

void ProcessTreeGuard::prepareProcess(QProcess* process) {
    if (!process)
        return;

    const pid_t parentPid = m_parentPid;
    process->setChildProcessModifier([parentPid] {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        // 防竞态：若父进程在 fork 后、prctl 前已退出，getppid() 会返回 init (1)
        if (getppid() != parentPid) {
            _exit(1);
        }
    });
}

bool ProcessTreeGuard::adoptProcess(QProcess* /*process*/) {
    // Linux: PDEATHSIG 已在 fork/exec 间由子进程设置，无需额外操作
    return true;
}

bool ProcessTreeGuard::isValid() const {
    return true;
}

void ProcessTreeGuard::invalidateForTesting() {
    // Linux: 无 handle 可失效，空实现
}

// ── 其他平台 ────────────────────────────────────────────────────────

#else

ProcessTreeGuard::ProcessTreeGuard() = default;
ProcessTreeGuard::~ProcessTreeGuard() = default;

void ProcessTreeGuard::prepareProcess(QProcess* /*process*/) {}
bool ProcessTreeGuard::adoptProcess(QProcess* /*process*/) { return true; }
bool ProcessTreeGuard::isValid() const { return true; }

void ProcessTreeGuard::invalidateForTesting() {}

#endif

} // namespace stdiolink

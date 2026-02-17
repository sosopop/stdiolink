// T10 辅助 stub — 检测自身是否受 OS 级进程树守护
// Windows: 输出 IsProcessInJob 结果
// Linux:   输出 prctl(PR_GET_PDEATHSIG) 结果
// 输出格式: "GUARD_STATUS:<value>\n"
//   Windows: value = 1 (in job) 或 0 (not in job)
//   Linux:   value = signal number (9 = SIGKILL)

#include <cstdio>

#ifdef _WIN32
#include <windows.h>

// IsProcessInJob 需要链接 kernel32（默认已链接）
int main() {
    BOOL inJob = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), NULL, &inJob)) {
        fprintf(stdout, "GUARD_STATUS:%d\n", inJob ? 1 : 0);
    } else {
        fprintf(stdout, "GUARD_STATUS:-1\n");
    }
    fflush(stdout);
    return 0;
}

#elif defined(__linux__)
#include <sys/prctl.h>
#include <signal.h>

int main() {
    int sig = 0;
    if (prctl(PR_GET_PDEATHSIG, &sig) == 0) {
        fprintf(stdout, "GUARD_STATUS:%d\n", sig);
    } else {
        fprintf(stdout, "GUARD_STATUS:-1\n");
    }
    fflush(stdout);
    return 0;
}

#else
// 其他平台：无守护机制
int main() {
    fprintf(stdout, "GUARD_STATUS:0\n");
    fflush(stdout);
    return 0;
}
#endif

#include "wait_any.h"
#include <QEventLoop>
#include <QTimer>
#include "driver.h"

namespace stdiolink {

bool waitAnyNext(QVector<Task>& tasks, AnyItem& out, int timeoutMs,
                 std::function<bool()> breakFlag) {
    // 1. 快速路径：先检查已有队列
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    // 2. 检查是否全部完成
    auto allDone = [&] {
        for (const auto& t : tasks) {
            if (t.isValid() && !t.isDone())
                return false;
        }
        return true;
    };
    if (allDone())
        return false;

    // 3. 先尝试 pump 所有 Driver
    for (auto& t : tasks) {
        if (t.isValid() && t.owner()) {
            t.owner()->pumpStdout();
        }
    }

    // 再次检查队列
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }
    if (allDone())
        return false;

    // 4. 使用 QEventLoop 等待
    QEventLoop loop;
    QTimer timer;
    QTimer breakTimer;

    auto quitIfReady = [&] {
        // pump 所有 Driver
        for (auto& t : tasks) {
            if (t.isValid() && t.owner()) {
                t.owner()->pumpStdout();
            }
        }
        // 检查是否有消息或全部完成
        for (const auto& t : tasks) {
            if (t.hasQueued()) {
                loop.quit();
                return;
            }
        }
        if (allDone()) {
            loop.quit();
        }
    };

    // 连接所有未完成 Task 对应 Driver 的信号
    QVector<QMetaObject::Connection> connections;
    for (auto& t : tasks) {
        if (t.isValid() && !t.isDone() && t.owner()) {
            auto conn1 = QObject::connect(t.owner()->process(), &QProcess::readyReadStandardOutput,
                                          &loop, quitIfReady);
            auto conn2 = QObject::connect(
                t.owner()->process(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                &loop, quitIfReady);
            connections.append(conn1);
            connections.append(conn2);
        }
    }

    // 设置超时
    if (timeoutMs >= 0) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
    }

    // 设置 breakFlag 检查定时器
    if (breakFlag) {
        breakTimer.setInterval(50); // 每 50ms 检查一次
        QObject::connect(&breakTimer, &QTimer::timeout, &loop, [&] {
            if (breakFlag()) {
                loop.quit();
            }
        });
        breakTimer.start();
    }

    loop.exec();

    // 断开所有连接
    for (const auto& conn : connections) {
        QObject::disconnect(conn);
    }

    // 5. loop 退出后再尝试取消息
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    return false;
}

} // namespace stdiolink

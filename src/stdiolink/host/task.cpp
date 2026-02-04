#include "task.h"
#include <QEventLoop>
#include <QTimer>
#include "driver.h"

namespace stdiolink {

Task::Task(Driver* owner, std::shared_ptr<TaskState> state) : drv(owner), st(std::move(state)) {}

bool Task::isValid() const {
    return drv != nullptr && st != nullptr;
}

bool Task::isDone() const {
    if (!st)
        return true; // 无效Task视为已完成
    return st->terminal && st->queue.empty();
}

int Task::exitCode() const {
    return (st && st->terminal) ? st->exitCode : -1;
}

QString Task::errorText() const {
    return st ? st->errorText : QString();
}

QJsonValue Task::finalPayload() const {
    return st ? st->finalPayload : QJsonValue();
}

bool Task::hasQueued() const {
    return st && !st->queue.empty();
}

bool Task::tryNext(Message& out) {
    if (!st || st->queue.empty())
        return false;
    out = std::move(st->queue.front());
    st->queue.pop_front();
    return true;
}

bool Task::waitNext(Message& out, int timeoutMs) {
    if (tryNext(out))
        return true;
    if (!st || !drv || isDone())
        return false;

    // 先尝试读取已有数据
    drv->pumpStdout();
    if (tryNext(out))
        return true;
    if (isDone())
        return false;

    QEventLoop loop;
    QTimer timer;

    auto quitIfReady = [&] {
        if (drv)
            drv->pumpStdout();
        if (hasQueued() || isDone())
            loop.quit();
    };

    QObject::connect(drv->process(), &QProcess::readyReadStandardOutput, &loop, quitIfReady);
    QObject::connect(drv->process(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, quitIfReady);

    if (timeoutMs >= 0) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
    }

    loop.exec();
    return tryNext(out);
}

} // namespace stdiolink

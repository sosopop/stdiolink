#include "task.h"
#include <QEventLoop>
#include <QTimer>
#include "driver.h"

namespace stdiolink {

Task::Task(Driver* owner, std::shared_ptr<TaskState> state) : m_drv(owner), m_st(std::move(state)) {}

bool Task::isValid() const {
    return m_drv != nullptr && m_st != nullptr;
}

bool Task::isDone() const {
    if (!m_st)
        return true; // 无效Task视为已完成
    return m_st->terminal && m_st->queue.empty();
}

int Task::exitCode() const {
    return (m_st && m_st->terminal) ? m_st->exitCode : -1;
}

QString Task::errorText() const {
    return m_st ? m_st->errorText : QString();
}

QJsonValue Task::finalPayload() const {
    return m_st ? m_st->finalPayload : QJsonValue();
}

bool Task::hasQueued() const {
    return m_st && !m_st->queue.empty();
}

bool Task::tryNext(Message& out) {
    if (!m_st || m_st->queue.empty())
        return false;
    out = std::move(m_st->queue.front());
    m_st->queue.pop_front();
    return true;
}

bool Task::waitNext(Message& out, int timeoutMs) {
    if (tryNext(out))
        return true;
    if (!m_st || !m_drv || isDone())
        return false;

    // 先尝试读取已有数据
    m_drv->pumpStdout();
    if (tryNext(out))
        return true;
    if (isDone())
        return false;

    QEventLoop loop;
    QTimer timer;

    auto quitIfReady = [&] {
        if (m_drv)
            m_drv->pumpStdout();
        if (hasQueued() || isDone())
            loop.quit();
    };

    QObject::connect(m_drv->process(), &QProcess::readyReadStandardOutput, &loop, quitIfReady);
    QObject::connect(m_drv->process(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
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

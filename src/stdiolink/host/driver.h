#pragma once

#include <QJsonObject>
#include <QProcess>
#include <memory>
#include "stdiolink/protocol/jsonl_types.h"
#include "task.h"

namespace stdiolink {

/**
 * Host 端 Driver 类
 * 管理一个 Driver 进程实例
 */
class Driver {
public:
    Driver() = default;
    ~Driver();

    // 禁止拷贝和移动（五法则）
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    Driver(Driver&&) = delete;
    Driver& operator=(Driver&&) = delete;

    bool start(const QString& program, const QStringList& args = {});
    void terminate();

    Task request(const QString& cmd, const QJsonObject& data = {});
    void pumpStdout();

    QProcess* process() { return &m_proc; }
    bool isRunning() const;
    bool hasQueued() const;
    bool isCurrentTerminal() const;

private:
    QProcess m_proc;
    QByteArray m_buf;

    bool m_waitingHeader = true;
    FrameHeader m_hdr;
    std::shared_ptr<TaskState> m_cur;

    bool tryReadLine(QByteArray& outLine);
    void pushError(int code, const QJsonObject& payload);
};

} // namespace stdiolink

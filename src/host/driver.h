#pragma once

#include "task.h"
#include "protocol/jsonl_types.h"
#include "protocol/jsonl_parser.h"
#include <QProcess>
#include <QJsonObject>
#include <memory>

namespace stdiolink {

/**
 * Host 端 Driver 类
 * 管理一个 Driver 进程实例
 */
class Driver {
public:
    Driver() = default;
    ~Driver();

    bool start(const QString& program, const QStringList& args = {});
    void terminate();

    Task request(const QString& cmd, const QJsonObject& data = {});
    void pumpStdout();

    QProcess* process() { return &proc; }
    bool isRunning() const;
    bool hasQueued() const;
    bool isCurrentTerminal() const;

private:
    QProcess proc;
    QByteArray buf;

    bool waitingHeader = true;
    FrameHeader hdr;
    std::shared_ptr<TaskState> cur;

    bool tryReadLine(QByteArray& outLine);
    void pushError(int code, const QJsonObject& payload);
};

} // namespace stdiolink

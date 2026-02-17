#pragma once

#include <QJsonObject>
#include <QProcess>
#include <memory>
#include "stdiolink/guard/process_guard_server.h"
#include "stdiolink/guard/process_tree_guard.h"
#include "stdiolink/protocol/jsonl_types.h"
#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"
#include "task.h"

namespace stdiolink {

/**
 * Host 端 Driver 类
 * 管理一个 Driver 进程实例
 */
class STDIOLINK_API Driver {
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
    QString exitContext() const;
    bool hasQueued() const;
    bool isCurrentTerminal() const;

    // 元数据查询
    const meta::DriverMeta* queryMeta(int timeoutMs = 5000);
    bool hasMeta() const;
    void refreshMeta();

#ifdef STDIOLINK_TESTING
public:
    void setGuardNameForTesting(const QString& name) { m_guardNameOverride = name; }
#endif

    static constexpr qint64 kMaxOutputBufferBytes = 8 * 1024 * 1024; // 8MB

private:
    QProcess m_proc;
    QByteArray m_buf;

    std::shared_ptr<TaskState> m_cur;
    std::shared_ptr<meta::DriverMeta> m_meta;
    std::unique_ptr<ProcessGuardServer> m_guard;
    ProcessTreeGuard m_treeGuard;
    QString m_guardNameOverride;

    bool tryReadLine(QByteArray& outLine);
    void pushError(int code, const QJsonObject& payload);
};

} // namespace stdiolink

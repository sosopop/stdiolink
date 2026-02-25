#pragma once

#include <QByteArray>
#include <QString>
#include <memory>
#include <spdlog/spdlog.h>

namespace stdiolink_server {

class InstanceLogWriter {
public:
    InstanceLogWriter(const QString& logPath,
                      qint64 maxBytes = 10 * 1024 * 1024,
                      int maxFiles = 3);
    ~InstanceLogWriter();

    void appendStdout(const QByteArray& data);
    void appendStderr(const QByteArray& data);
    QString logPath() const { return m_logPath; }

private:
    void processBuffer(QByteArray& buf, const char* prefix);

    std::shared_ptr<spdlog::logger> m_logger;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    QString m_logPath;

    static constexpr qint64 kMaxBufferBytes = 1 * 1024 * 1024;  // 1MB
};

} // namespace stdiolink_server

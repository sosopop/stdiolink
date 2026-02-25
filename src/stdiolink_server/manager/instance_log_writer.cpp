#include "instance_log_writer.h"

#include <spdlog/sinks/rotating_file_sink.h>

namespace stdiolink_server {

InstanceLogWriter::InstanceLogWriter(const QString& logPath,
                                     qint64 maxBytes, int maxFiles)
    : m_logPath(logPath)
{
    try {
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.toStdString(),
            static_cast<size_t>(maxBytes),
            static_cast<size_t>(maxFiles));

        const std::string loggerName = "inst_" + logPath.toStdString();
        m_logger = std::make_shared<spdlog::logger>(loggerName, sink);
        m_logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ | %v",
                              spdlog::pattern_time_type::utc);
        m_logger->set_level(spdlog::level::trace);
        m_logger->flush_on(spdlog::level::trace);
    } catch (const spdlog::spdlog_ex& ex) {
        qWarning("InstanceLogWriter: failed to create logger for %s: %s",
                 qPrintable(logPath), ex.what());
    }
}

InstanceLogWriter::~InstanceLogWriter() {
    if (!m_logger) return;
    if (!m_stdoutBuf.isEmpty()) {
        m_logger->info("{}", m_stdoutBuf.constData());
    }
    if (!m_stderrBuf.isEmpty()) {
        m_logger->info("[stderr] {}", m_stderrBuf.constData());
    }
    spdlog::drop(m_logger->name());
}

void InstanceLogWriter::processBuffer(QByteArray& buf, const char* prefix) {
    if (!m_logger) { buf.clear(); return; }
    while (true) {
        const int nl = buf.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = buf.left(nl);
        buf.remove(0, nl + 1);
        if (prefix) {
            m_logger->info("{} {}", prefix, line.constData());
        } else {
            m_logger->info("{}", line.constData());
        }
    }
}

void InstanceLogWriter::appendStdout(const QByteArray& data) {
    m_stdoutBuf.append(data);
    processBuffer(m_stdoutBuf, nullptr);
    if (m_stdoutBuf.size() > kMaxBufferBytes) {
        if (m_logger) m_logger->info("{}", m_stdoutBuf.constData());
        m_stdoutBuf.clear();
    }
}

void InstanceLogWriter::appendStderr(const QByteArray& data) {
    m_stderrBuf.append(data);
    processBuffer(m_stderrBuf, "[stderr]");
    if (m_stderrBuf.size() > kMaxBufferBytes) {
        if (m_logger) m_logger->info("[stderr] {}", m_stderrBuf.constData());
        m_stderrBuf.clear();
    }
}

} // namespace stdiolink_server

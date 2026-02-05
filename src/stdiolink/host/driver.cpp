#include "driver.h"
#include <QJsonDocument>
#include "meta_cache.h"
#include "stdiolink/protocol/jsonl_serializer.h"

namespace stdiolink {

Driver::~Driver() {
    terminate();
}

bool Driver::start(const QString& program, const QStringList& args) {
    m_proc.setProgram(program);
    m_proc.setArguments(args);
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.start();
    return m_proc.waitForStarted(3000);
}

void Driver::terminate() {
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.terminate();
        if (!m_proc.waitForFinished(1000)) {
            m_proc.kill();
        }
    }
}

bool Driver::isRunning() const {
    return m_proc.state() == QProcess::Running;
}

Task Driver::request(const QString& cmd, const QJsonObject& data) {
    m_cur = std::make_shared<TaskState>();

    QJsonObject req;
    req["cmd"] = cmd;
    if (!data.isEmpty())
        req["data"] = data;

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_proc.write(line);
    m_proc.waitForBytesWritten(1000);

    m_buf.clear();

    return {this, m_cur};
}

bool Driver::hasQueued() const {
    return m_cur && !m_cur->queue.empty();
}

bool Driver::isCurrentTerminal() const {
    return m_cur && m_cur->terminal;
}

bool Driver::tryReadLine(QByteArray& outLine) {
    int idx = m_buf.indexOf('\n');
    if (idx < 0)
        return false;
    outLine = m_buf.left(idx);
    m_buf.remove(0, idx + 1);
    return true;
}

void Driver::pushError(int code, const QJsonObject& payload) {
    Message msg{"error", code, payload};
    m_cur->queue.push_back(msg);
    m_cur->terminal = true;
    m_cur->exitCode = code;
    m_cur->finalPayload = payload;

    if (payload.contains("message")) {
        m_cur->errorText = payload["message"].toString();
    }
}

void Driver::pumpStdout() {
    if (!m_cur)
        return;

    m_buf.append(m_proc.readAllStandardOutput());

    QByteArray line;
    while (tryReadLine(line)) {
        Message msg;
        if (!parseResponse(line, msg)) {
            pushError(1000, QJsonObject{{"message", "invalid response"},
                                        {"raw", QString::fromUtf8(line)}});
            return;
        }

        m_cur->queue.push_back(msg);

        if (msg.status == "done" || msg.status == "error") {
            m_cur->terminal = true;
            m_cur->exitCode = msg.code;
            m_cur->finalPayload = msg.payload;

            if (msg.status == "error" && msg.payload.isObject()) {
                auto obj = msg.payload.toObject();
                if (obj.contains("message")) {
                    m_cur->errorText = obj["message"].toString();
                }
            }
        }
    }
}

const meta::DriverMeta* Driver::queryMeta(int timeoutMs) {
    if (m_meta) {
        return m_meta.get();
    }

    // 发送 meta.describe 请求
    Task task = request("meta.describe", QJsonObject{});
    Message msg;
    if (!task.waitNext(msg, timeoutMs)) {
        return nullptr;
    }

    if (msg.status != "done") {
        return nullptr;
    }

    // 解析元数据
    m_meta = std::make_shared<meta::DriverMeta>(
        meta::DriverMeta::fromJson(msg.payload.toObject()));

    // 存入缓存
    if (!m_meta->info.id.isEmpty()) {
        MetaCache::instance().store(m_meta->info.id, m_meta);
    }

    return m_meta.get();
}

bool Driver::hasMeta() const {
    return m_meta != nullptr;
}

void Driver::refreshMeta() {
    if (m_meta && !m_meta->info.id.isEmpty()) {
        MetaCache::instance().invalidate(m_meta->info.id);
    }
    m_meta.reset();
}

} // namespace stdiolink

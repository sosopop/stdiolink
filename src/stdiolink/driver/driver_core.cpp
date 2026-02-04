#include "driver_core.h"
#include <QFile>
#include <QJsonObject>
#include <QTextStream>
#include "meta_command_handler.h"
#include "stdio_responder.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include "stdiolink/protocol/meta_validator.h"

namespace stdiolink {

int DriverCore::run() {
    if (!m_handler) {
        return 1;
    }

    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    QTextStream in(&input);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty())
            continue;

        if (!processOneLine(line.toUtf8())) {
            // 处理失败，继续下一行
        }

        if (m_profile == Profile::OneShot) {
            break;
        }
    }

    return 0;
}

void DriverCore::setMetaHandler(IMetaCommandHandler* h) {
    m_metaHandler = h;
    m_handler = h;  // 同时设置为普通处理器
}

bool DriverCore::processOneLine(const QByteArray& line) {
    // 跳过空行
    if (line.trimmed().isEmpty()) {
        return true;
    }

    // 解析请求
    Request req;
    if (!parseRequest(line, req)) {
        StdioResponder responder;
        responder.error(1000, QJsonObject{{"message", "invalid request format"},
                                          {"raw", QString::fromUtf8(line)}});
        return false;
    }

    // 处理命令
    StdioResponder responder;

    // 优先处理 meta 命令
    if (handleMetaCommand(req.cmd, req.data, responder)) {
        return true;
    }

    // 自动参数验证
    if (m_metaHandler && m_metaHandler->autoValidateParams()) {
        const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(req.cmd);
        if (cmdMeta) {
            // 填充默认值
            QJsonObject filledData = meta::DefaultFiller::fillDefaults(
                req.data.toObject(), *cmdMeta);

            // 验证参数
            auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
            if (!result.valid) {
                responder.error(400, QJsonObject{
                    {"name", "ValidationFailed"},
                    {"message", result.toString()}
                });
                return false;
            }

            // 使用填充后的数据调用处理器
            m_handler->handle(req.cmd, filledData, responder);
            return true;
        }
    }

    m_handler->handle(req.cmd, req.data, responder);
    return true;
}

bool DriverCore::handleMetaCommand(const QString& cmd, const QJsonValue& data,
                                   IResponder& responder) {
    Q_UNUSED(data)

    if (!cmd.startsWith("meta.")) {
        return false;
    }

    if (m_metaHandler == nullptr) {
        responder.error(501, QJsonObject{
            {"name", "MetaNotSupported"},
            {"message", "This driver does not support meta commands"}
        });
        return true;
    }

    if (cmd == "meta.describe") {
        QJsonObject metaJson = m_metaHandler->driverMeta().toJson();
        responder.done(0, metaJson);
        return true;
    }

    // 未知的 meta 命令
    responder.error(404, QJsonObject{
        {"name", "CommandNotFound"},
        {"message", QString("Unknown meta command: %1").arg(cmd)}
    });
    return true;
}

} // namespace stdiolink

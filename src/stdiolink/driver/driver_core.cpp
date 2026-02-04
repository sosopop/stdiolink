#include "driver_core.h"
#include <QFile>
#include <QJsonObject>
#include <QTextStream>
#include "stdio_responder.h"
#include "stdiolink/protocol/jsonl_serializer.h"

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
    m_handler->handle(req.cmd, req.data, responder);
    return true;
}

} // namespace stdiolink

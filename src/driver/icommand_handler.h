#pragma once

#include "iresponder.h"
#include <QString>
#include <QJsonValue>

namespace stdiolink {

/**
 * 命令处理器接口
 * 业务层实现此接口来处理具体命令
 */
class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    /**
     * 处理命令
     * @param cmd 命令名
     * @param data 命令数据
     * @param responder 响应输出接口
     */
    virtual void handle(const QString& cmd,
                       const QJsonValue& data,
                       IResponder& responder) = 0;
};

} // namespace stdiolink

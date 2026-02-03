#pragma once

#include <QString>
#include <QJsonValue>

namespace stdiolink {

/**
 * 响应输出接口
 * Driver 通过此接口输出响应消息
 */
class IResponder {
public:
    virtual ~IResponder() = default;

    /**
     * 输出中间事件
     */
    virtual void event(int code, const QJsonValue& payload) = 0;

    /**
     * 输出成功完成
     */
    virtual void done(int code, const QJsonValue& payload) = 0;

    /**
     * 输出错误
     */
    virtual void error(int code, const QJsonValue& payload) = 0;
};

} // namespace stdiolink

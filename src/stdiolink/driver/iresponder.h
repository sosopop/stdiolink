#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace stdiolink {

/**
 * 响应输出接口
 * Driver 通过此接口输出响应消息
 */
class IResponder {
public:
    virtual ~IResponder() = default;

    /**
     * 输出中间事件（旧版接口，保留兼容）
     * 内部自动封装为 {event:"default", data:payload}
     */
    virtual void event(int code, const QJsonValue& payload) = 0;

    /**
     * 输出带事件名的中间事件（M15 新增）
     * @param eventName 事件名称
     * @param code 事件代码
     * @param data 事件数据
     */
    virtual void event(const QString& eventName, int code, const QJsonValue& data) {
        // 默认实现：封装为标准格式后调用旧版接口
        QJsonObject payload;
        payload["event"] = eventName;
        payload["data"] = data;
        event(code, payload);
    }

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

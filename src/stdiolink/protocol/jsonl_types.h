#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace stdiolink {

/**
 * 请求结构（Host → Driver）
 */
struct Request {
    QString cmd;     // 命令名（必填）
    QJsonValue data; // 数据（可选）
};

/**
 * 响应头结构
 */
struct FrameHeader {
    QString status; // "event" | "done" | "error"
    int code = 0;   // 错误码，0 表示成功
};

/**
 * 完整消息结构（Host 侧使用）
 */
struct Message {
    QString status;     // "event" | "done" | "error"
    int code = 0;       // 错误码
    QJsonValue payload; // 数据载荷
};

} // namespace stdiolink

#pragma once

#include "stdiolink/protocol/jsonl_types.h"
#include <deque>
#include <memory>
#include <QString>
#include <QJsonValue>

namespace stdiolink {

/**
 * Task 状态结构
 * 存储一次请求的状态和消息队列
 */
struct TaskState {
    bool terminal = false;        // 是否已收到 done/error
    int exitCode = 0;             // 终态 code
    QString errorText;            // 错误文本
    QJsonValue finalPayload;      // 终态 payload
    std::deque<Message> queue;    // 待取消息队列
};

} // namespace stdiolink

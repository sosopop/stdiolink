#pragma once

#include <QVector>
#include <functional>
#include "stdiolink/stdiolink_export.h"
#include "task.h"

namespace stdiolink {

/**
 * waitAnyNext 返回的结果
 */
struct STDIOLINK_API AnyItem {
    int taskIndex = -1; // 来源 Task 在数组中的索引
    Message msg;        // 消息内容
};

/**
 * 等待任意一个 Task 产生新消息
 */
STDIOLINK_API bool waitAnyNext(QVector<Task>& tasks, AnyItem& out, int timeoutMs = -1,
                               std::function<bool()> breakFlag = {});

} // namespace stdiolink

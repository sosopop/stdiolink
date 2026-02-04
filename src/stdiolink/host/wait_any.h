#pragma once

#include <QVector>
#include <functional>
#include "task.h"

namespace stdiolink {

/**
 * waitAnyNext 返回的结果
 */
struct AnyItem {
    int taskIndex = -1; // 来源 Task 在数组中的索引
    Message msg;        // 消息内容
};

/**
 * 等待任意一个 Task 产生新消息
 * @param tasks 任务列表
 * @param out 输出的消息
 * @param timeoutMs 超时时间（毫秒），-1 表示无限等待
 * @param breakFlag 中断标志函数，返回 true 时中断等待
 * @return 是否成功获取到消息
 */
bool waitAnyNext(QVector<Task>& tasks, AnyItem& out, int timeoutMs = -1,
                 std::function<bool()> breakFlag = {});

} // namespace stdiolink

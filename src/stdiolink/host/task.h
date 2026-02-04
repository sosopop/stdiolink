#pragma once

#include <memory>
#include "task_state.h"

namespace stdiolink {

class Driver;

/**
 * Task 句柄（Future/Promise 风格）
 * 代表一次请求的结果
 */
class Task {
public:
    Task() = default;
    Task(Driver* owner, std::shared_ptr<TaskState> state);

    bool isValid() const;
    bool isDone() const;
    int exitCode() const;
    QString errorText() const;
    QJsonValue finalPayload() const;

    bool tryNext(Message& out);
    bool waitNext(Message& out, int timeoutMs = -1);
    bool hasQueued() const;

    Driver* owner() const { return drv; }

private:
    Driver* drv = nullptr;
    std::shared_ptr<TaskState> st;
};

} // namespace stdiolink

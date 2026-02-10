#pragma once

#include <memory>
#include "stdiolink/stdiolink_export.h"
#include "task_state.h"

namespace stdiolink {

class Driver;

/**
 * Task 句柄（Future/Promise 风格）
 * 代表一次请求的结果
 */
class STDIOLINK_API Task {
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
    const TaskState* stateId() const { return m_st.get(); }
    void forceTerminal(int code, const QString& error);

    Driver* owner() const { return m_drv; }

private:
    Driver* m_drv = nullptr;
    std::shared_ptr<TaskState> m_st;
};

} // namespace stdiolink

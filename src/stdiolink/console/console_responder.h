#pragma once

#include "stdiolink/driver/iresponder.h"

namespace stdiolink {

/**
 * Console 模式响应器
 * - done/error 输出到 stdout
 * - event 输出到 stderr（可选）
 */
class ConsoleResponder : public IResponder {
public:
    void event(int code, const QJsonValue& payload) override;
    void done(int code, const QJsonValue& payload) override;
    void error(int code, const QJsonValue& payload) override;

    int exitCode() const { return exitCode_; }
    bool hasResult() const { return hasResult_; }

private:
    void writeToStdout(const QString& status, int code, const QJsonValue& payload);
    void writeToStderr(const QString& status, int code, const QJsonValue& payload);

    int exitCode_ = 0;
    bool hasResult_ = false;
};

} // namespace stdiolink

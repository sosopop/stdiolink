#pragma once

#include "stdiolink/stdiolink_export.h"
#include "stdiolink/driver/iresponder.h"

namespace stdiolink {

/**
 * Console 模式响应器
 */
class STDIOLINK_API ConsoleResponder : public IResponder {
public:
    void event(int code, const QJsonValue& payload) override;
    void event(const QString& eventName, int code, const QJsonValue& data) override;
    void done(int code, const QJsonValue& payload) override;
    void error(int code, const QJsonValue& payload) override;

    int exitCode() const { return m_exitCode; }
    bool hasResult() const { return m_hasResult; }

private:
    static void writeToStdout(const QString& status, int code, const QJsonValue& payload);

    int m_exitCode = 0;
    bool m_hasResult = false;
};

} // namespace stdiolink

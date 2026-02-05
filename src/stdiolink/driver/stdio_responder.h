#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonValue>
#include "iresponder.h"

namespace stdiolink {

/**
 * StdIO 响应器
 */
class STDIOLINK_API StdioResponder : public IResponder {
public:
    StdioResponder() = default;

    void event(int code, const QJsonValue& payload) override;
    void event(const QString& eventName, int code, const QJsonValue& data) override;
    void done(int code, const QJsonValue& payload) override;
    void error(int code, const QJsonValue& payload) override;

private:
    static void writeResponse(const QString& status, int code, const QJsonValue& payload);
};

} // namespace stdiolink

#pragma once

#include <QJsonValue>
#include "iresponder.h"

namespace stdiolink {

/**
 * StdIO 响应器
 * 将响应输出到 stdout
 */
class StdioResponder : public IResponder {
public:
    StdioResponder() = default;

    void event(int code, const QJsonValue& payload) override;
    void done(int code, const QJsonValue& payload) override;
    void error(int code, const QJsonValue& payload) override;

private:
    static void writeResponse(const QString& status, int code, const QJsonValue& payload);
};

} // namespace stdiolink

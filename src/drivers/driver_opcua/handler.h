#pragma once

#include <QJsonObject>
#include <QString>

#include "stdiolink/driver/meta_command_handler.h"

struct OpcUaConnectionOptions {
    QString host;
    quint16 port = 4840;
    int timeoutMs = 3000;
};

class OpcUaHandler : public stdiolink::IMetaCommandHandler {
public:
    OpcUaHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& responder) override;

    static bool resolveConnectionOptions(const QJsonObject& params,
                                         OpcUaConnectionOptions& options,
                                         QString* errorMessage = nullptr);
    static bool normalizeNodeId(const QString& input,
                                QString& normalizedNodeId,
                                QString* errorMessage = nullptr);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
};

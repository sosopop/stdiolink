#pragma once

#include <mutex>

#include "driver_opcua_server/opcua_server_runtime.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/stdio_responder.h"

class OpcUaServerHandler : public stdiolink::IMetaCommandHandler {
public:
    OpcUaServerHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& responder) override;

    void setEventResponder(stdiolink::IResponder* responder);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
    stdiolink::StdioResponder m_stdioResponder;
    stdiolink::IResponder* m_eventResponder = nullptr;
    std::recursive_mutex m_outputMutex;
    OpcUaServerRuntime m_runtime;
};

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>

#include "driver_modbustcp_server/modbus_tcp_server.h"
#include "sim_device.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/stdio_responder.h"

struct SimRunConfig {
    QString listenAddress;
    int listenPort = 502;
    quint8 unitId = 1;
    int dataAreaSize = 256;

    int cylinderUpDelayMs = 2500;
    int cylinderDownDelayMs = 2000;
    int valveOpenDelayMs = 1500;
    int valveCloseDelayMs = 1200;
    int tickMs = 50;

    QString eventMode = "write";
    int heartbeatMs = 0;
};

class SimPlcCraneHandler : public stdiolink::IMetaCommandHandler {
public:
    SimPlcCraneHandler();
    ~SimPlcCraneHandler() override;

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& resp) override;

    bool isRunning() const { return m_running; }
    quint16 serverPort() const { return m_server.serverPort(); }
    int uptimeSeconds() const;
#ifdef STDIOLINK_TESTING
    SimPlcCraneDevice& device() { return m_device; }
    const SimPlcCraneDevice& device() const { return m_device; }
    bool writeHoldingRegisterForTest(quint16 address, quint16 value, QString& err);
    bool readHoldingRegisterForTest(quint16 address, quint16& value);
#endif

private:
    void buildMeta();
    void handleRun(const QJsonObject& data, stdiolink::IResponder& resp);
    void syncDataAreaFromDevice();
    void onDataWritten(quint8 unitId, quint8 functionCode, quint16 address, quint16 quantity);
    bool eventReadEnabled() const;
    bool eventWriteEnabled() const;

    stdiolink::meta::DriverMeta m_meta;
    SimRunConfig m_cfg;
    SimPlcCraneDevice m_device;
    ModbusTcpServer m_server;
    stdiolink::StdioResponder m_eventResponder;

    QTimer m_refreshTimer;
    QTimer m_heartbeatTimer;
    bool m_running = false;
    QElapsedTimer m_uptimeClock;
};

#ifndef MODBUS_RTU_CLIENT_H
#define MODBUS_RTU_CLIENT_H

#include <QTcpSocket>
#include <QHash>
#include <QString>
#include <QVector>
#include <memory>
#include "modbus_types.h"

namespace modbus {

/**
 * Modbus RTU Over TCP 请求结果
 */
struct ModbusResult {
    bool success = false;
    ExceptionCode exception = ExceptionCode::None;
    QString errorMessage;
    QVector<bool> coils;
    QVector<uint16_t> registers;
};

/**
 * 连接信息（用于连接池）
 */
struct ConnectionKey {
    QString host;
    quint16 port;

    bool operator==(const ConnectionKey& other) const {
        return host == other.host && port == other.port;
    }
};

inline uint qHash(const ConnectionKey& key, uint seed = 0) {
    return qHash(key.host, seed) ^ (key.port + seed);
}

/**
 * Modbus RTU Over TCP 客户端
 *
 * RTU 帧格式: [Unit ID (1)] [FC (1)] [Data (N)] [CRC16 (2)]
 * 与 Modbus TCP 的区别:
 * - 无 MBAP Header
 * - 使用 CRC16 校验
 */
class ModbusRtuClient {
public:
    explicit ModbusRtuClient(int timeout = 3000);
    ~ModbusRtuClient();

    // 连接管理
    bool connectToServer(const QString& host, quint16 port);
    void disconnect();
    bool isConnected() const;

    // 设置
    void setTimeout(int ms) { m_timeout = ms; }
    void setUnitId(uint8_t id) { m_unitId = id; }

    // 功能码 0x01: 读线圈
    ModbusResult readCoils(uint16_t address, uint16_t count);

    // 功能码 0x02: 读离散输入
    ModbusResult readDiscreteInputs(uint16_t address, uint16_t count);

    // 功能码 0x03: 读保持寄存器
    ModbusResult readHoldingRegisters(uint16_t address, uint16_t count);

    // 功能码 0x04: 读输入寄存器
    ModbusResult readInputRegisters(uint16_t address, uint16_t count);

    // 功能码 0x05: 写单个线圈
    ModbusResult writeSingleCoil(uint16_t address, bool value);

    // 功能码 0x06: 写单个寄存器
    ModbusResult writeSingleRegister(uint16_t address, uint16_t value);

    // 功能码 0x0F: 写多个线圈
    ModbusResult writeMultipleCoils(uint16_t address, const QVector<bool>& values);

    // 功能码 0x10: 写多个寄存器
    ModbusResult writeMultipleRegisters(uint16_t address, const QVector<uint16_t>& values);

    // CRC16 计算（公开用于测试）
    static uint16_t calculateCRC16(const QByteArray& data);

private:
    QByteArray buildRequest(FunctionCode fc, const QByteArray& pdu);
    ModbusResult parseReadBitsResponse(const QByteArray& response, uint16_t count);
    ModbusResult parseReadRegistersResponse(const QByteArray& response);
    ModbusResult parseWriteResponse(const QByteArray& response);
    bool verifyCRC(const QByteArray& frame);

    QTcpSocket m_socket;
    uint8_t m_unitId = 1;
    int m_timeout = 3000;
};

} // namespace modbus

#endif // MODBUS_RTU_CLIENT_H

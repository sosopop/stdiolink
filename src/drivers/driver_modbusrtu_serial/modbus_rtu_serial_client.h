#ifndef MODBUS_RTU_SERIAL_CLIENT_H
#define MODBUS_RTU_SERIAL_CLIENT_H

#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QVector>
#include "modbus_types.h"

using namespace modbus;

struct SerialModbusResult {
    bool success = false;
    ExceptionCode exception = ExceptionCode::None;
    QString errorMessage;
    QVector<bool> coils;
    QVector<uint16_t> registers;
};

class ModbusRtuSerialClient {
public:
    ModbusRtuSerialClient();
    ~ModbusRtuSerialClient();

    bool open(const QString& portName, int baudRate = 9600,
              int dataBits = 8, const QString& stopBits = "1",
              const QString& parity = "none");
    void close();
    bool isOpen() const;

    SerialModbusResult readCoils(quint8 unitId, quint16 address,
                                 quint16 count, int timeout = 3000);
    SerialModbusResult readDiscreteInputs(quint8 unitId, quint16 address,
                                           quint16 count, int timeout = 3000);
    SerialModbusResult readHoldingRegisters(quint8 unitId, quint16 address,
                                             quint16 count, int timeout = 3000);
    SerialModbusResult readInputRegisters(quint8 unitId, quint16 address,
                                           quint16 count, int timeout = 3000);
    SerialModbusResult writeSingleCoil(quint8 unitId, quint16 address,
                                        bool value, int timeout = 3000);
    SerialModbusResult writeMultipleCoils(quint8 unitId, quint16 address,
                                           const QVector<bool>& values, int timeout = 3000);
    SerialModbusResult writeSingleRegister(quint8 unitId, quint16 address,
                                            quint16 value, int timeout = 3000);
    SerialModbusResult writeMultipleRegisters(quint8 unitId, quint16 address,
                                               const QVector<quint16>& values, int timeout = 3000);

    static uint16_t calculateCRC16(const QByteArray& data);
    static double calculateT35(int baudRate, int dataBits,
                                bool hasParity, double stopBits);

private:
    QByteArray buildRequest(quint8 unitId, quint8 fc, const QByteArray& pdu);
    QByteArray sendRequest(const QByteArray& request, int timeout);
    SerialModbusResult parseResponse(const QByteArray& response, quint8 expectedUnitId, quint8 expectedFc, quint16 bitCount = 0);
    bool verifyCRC(const QByteArray& frame);

    QSerialPort* m_serial = nullptr;
    double m_t35Ms = 3.646;
};

#endif // MODBUS_RTU_SERIAL_CLIENT_H

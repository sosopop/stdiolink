#include "modbus_client.h"
#include <QDataStream>
#include <QElapsedTimer>

namespace modbus {

namespace {

quint16 readUInt16BE(const QByteArray& data, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(data[offset]) << 8) |
                                static_cast<quint8>(data[offset + 1]));
}

} // namespace

ModbusClient::ModbusClient(int timeout)
    : m_timeout(timeout)
{
}

ModbusClient::~ModbusClient()
{
    disconnect();
}

bool ModbusClient::connectToServer(const QString& host, quint16 port)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        if (m_socket.peerAddress().toString() == host && m_socket.peerPort() == port) {
            return true; // 已连接到同一服务器
        }
        disconnect();
    }

    m_socket.connectToHost(host, port);
    return m_socket.waitForConnected(m_timeout);
}

void ModbusClient::disconnect()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(1000);
        }
    }
}

bool ModbusClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

QByteArray ModbusClient::buildRequest(FunctionCode fc, const QByteArray& pdu)
{
    QByteArray request;
    QDataStream stream(&request, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // MBAP Header
    stream << m_transactionId++;           // Transaction ID
    stream << quint16(0);                  // Protocol ID (0 = Modbus)
    stream << quint16(pdu.size() + 2);     // Length (Unit ID + PDU)
    stream << m_unitId;                    // Unit ID

    // PDU
    stream << static_cast<uint8_t>(fc);
    request.append(pdu);

    return request;
}

QByteArray ModbusClient::readResponse(quint16 expectedTransactionId, QString& errorMessage)
{
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    int expectedLength = -1;

    while (timer.elapsed() < m_timeout) {
        if (expectedLength > 0 && response.size() >= expectedLength) {
            return response.left(expectedLength);
        }

        const int remaining = m_timeout - static_cast<int>(timer.elapsed());
        const int waitMs = remaining > 100 ? 100 : remaining;
        if (waitMs <= 0) {
            break;
        }

        if (!m_socket.waitForReadyRead(waitMs)) {
            continue;
        }

        response.append(m_socket.readAll());

        if (expectedLength < 0 && response.size() >= 7) {
            const quint16 transactionId = readUInt16BE(response, 0);
            if (transactionId != expectedTransactionId) {
                errorMessage = "Unexpected transaction id";
                return {};
            }

            const quint16 protocolId = readUInt16BE(response, 2);
            if (protocolId != 0) {
                errorMessage = "Unexpected protocol id";
                return {};
            }

            const quint16 length = readUInt16BE(response, 4);
            if (length < 2) {
                errorMessage = "Invalid MBAP length";
                return {};
            }

            expectedLength = 6 + length;
        }
    }

    if (expectedLength > 0 && response.size() < expectedLength) {
        errorMessage = "Response truncated";
    } else if (response.isEmpty()) {
        errorMessage = "Read timeout";
    } else {
        errorMessage = "Response too short";
    }

    return {};
}

ModbusResult ModbusClient::sendRequest(const QByteArray& request, FunctionCode expectedFc,
                                       QByteArray* responseOut)
{
    ModbusResult result;

    if (!isConnected()) {
        result.errorMessage = "Not connected";
        return result;
    }

    // 发送请求
    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(m_timeout)) {
        result.errorMessage = "Write timeout";
        return result;
    }

    const quint16 expectedTransactionId = readUInt16BE(request, 0);
    const QByteArray response = readResponse(expectedTransactionId, result.errorMessage);
    if (response.isEmpty()) {
        return result;
    }

    if (response.size() < 8) {
        result.errorMessage = "Response too short";
        return result;
    }

    // 检查功能码
    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        // 异常响应
        result.exception = static_cast<ExceptionCode>(response[8]);
        result.errorMessage = exceptionMessage(result.exception);
        return result;
    }

    if (fc != static_cast<uint8_t>(expectedFc)) {
        result.errorMessage = "Unexpected function code";
        return result;
    }

    result.success = true;
    if (responseOut) {
        *responseOut = response;
    }
    return result;
}

ModbusResult ModbusClient::parseReadBitsResponse(const QByteArray& response, uint16_t count)
{
    ModbusResult result;
    result.success = true;

    if (response.size() < 9) {
        result.success = false;
        result.errorMessage = "Response too short for bit data";
        return result;
    }

    uint8_t byteCount = static_cast<uint8_t>(response[8]);
    if (response.size() < 9 + byteCount) {
        result.success = false;
        result.errorMessage = "Incomplete bit data";
        return result;
    }

    for (uint16_t i = 0; i < count; ++i) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        bool value = (response[9 + byteIndex] >> bitIndex) & 0x01;
        result.coils.append(value);
    }

    return result;
}

ModbusResult ModbusClient::parseReadRegistersResponse(const QByteArray& response)
{
    ModbusResult result;
    result.success = true;

    if (response.size() < 9) {
        result.success = false;
        result.errorMessage = "Response too short for register data";
        return result;
    }

    uint8_t byteCount = static_cast<uint8_t>(response[8]);
    if (response.size() < 9 + byteCount) {
        result.success = false;
        result.errorMessage = "Incomplete register data";
        return result;
    }

    int regCount = byteCount / 2;
    for (int i = 0; i < regCount; ++i) {
        uint16_t value = (static_cast<uint8_t>(response[9 + i*2]) << 8) |
                          static_cast<uint8_t>(response[9 + i*2 + 1]);
        result.registers.append(value);
    }

    return result;
}

ModbusResult ModbusClient::parseWriteResponse(const QByteArray& response)
{
    ModbusResult result;
    result.success = (response.size() >= 12);
    if (!result.success) {
        result.errorMessage = "Write response too short";
    }
    return result;
}

ModbusResult ModbusClient::readCoils(uint16_t address, uint16_t count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << count;

    QByteArray request = buildRequest(FunctionCode::ReadCoils, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::ReadCoils, &response);
    if (!result.success) {
        return result;
    }
    return parseReadBitsResponse(response, count);
}

ModbusResult ModbusClient::readDiscreteInputs(uint16_t address, uint16_t count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << count;

    QByteArray request = buildRequest(FunctionCode::ReadDiscreteInputs, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::ReadDiscreteInputs, &response);
    if (!result.success) {
        return result;
    }
    return parseReadBitsResponse(response, count);
}

ModbusResult ModbusClient::readHoldingRegisters(uint16_t address, uint16_t count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << count;

    QByteArray request = buildRequest(FunctionCode::ReadHoldingRegisters, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::ReadHoldingRegisters, &response);
    if (!result.success) {
        return result;
    }
    return parseReadRegistersResponse(response);
}

ModbusResult ModbusClient::readInputRegisters(uint16_t address, uint16_t count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << count;

    QByteArray request = buildRequest(FunctionCode::ReadInputRegisters, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::ReadInputRegisters, &response);
    if (!result.success) {
        return result;
    }
    return parseReadRegistersResponse(response);
}

ModbusResult ModbusClient::writeSingleCoil(uint16_t address, bool value)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << quint16(value ? 0xFF00 : 0x0000);

    QByteArray request = buildRequest(FunctionCode::WriteSingleCoil, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::WriteSingleCoil, &response);
    if (!result.success) {
        return result;
    }
    return parseWriteResponse(response);
}

ModbusResult ModbusClient::writeSingleRegister(uint16_t address, uint16_t value)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << value;

    QByteArray request = buildRequest(FunctionCode::WriteSingleRegister, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::WriteSingleRegister, &response);
    if (!result.success) {
        return result;
    }
    return parseWriteResponse(response);
}

ModbusResult ModbusClient::writeMultipleCoils(uint16_t address, const QVector<bool>& values)
{
    int byteCount = (values.size() + 7) / 8;
    QByteArray coilData(byteCount, 0);

    for (int i = 0; i < values.size(); ++i) {
        if (values[i]) {
            coilData[i / 8] |= (1 << (i % 8));
        }
    }

    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << quint16(values.size()) << quint8(byteCount);
    pdu.append(coilData);

    QByteArray request = buildRequest(FunctionCode::WriteMultipleCoils, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::WriteMultipleCoils, &response);
    if (!result.success) {
        return result;
    }
    return parseWriteResponse(response);
}

ModbusResult ModbusClient::writeMultipleRegisters(uint16_t address, const QVector<uint16_t>& values)
{
    QByteArray regData;
    QDataStream regStream(&regData, QIODevice::WriteOnly);
    regStream.setByteOrder(QDataStream::BigEndian);
    for (uint16_t v : values) {
        regStream << v;
    }

    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << address << quint16(values.size()) << quint8(values.size() * 2);
    pdu.append(regData);

    QByteArray request = buildRequest(FunctionCode::WriteMultipleRegisters, pdu);
    QByteArray response;
    ModbusResult result = sendRequest(request, FunctionCode::WriteMultipleRegisters, &response);
    if (!result.success) {
        return result;
    }
    return parseWriteResponse(response);
}

} // namespace modbus

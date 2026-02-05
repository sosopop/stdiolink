#include "modbus_client.h"
#include <QDataStream>

namespace modbus {

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

ModbusResult ModbusClient::sendRequest(const QByteArray& request, FunctionCode expectedFc)
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

    // 等待响应
    if (!m_socket.waitForReadyRead(m_timeout)) {
        result.errorMessage = "Read timeout";
        return result;
    }

    QByteArray response = m_socket.readAll();

    // 最小响应长度: MBAP(7) + FC(1) = 8
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }

    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
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
    m_socket.write(request);

    if (!m_socket.waitForBytesWritten(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Write timeout", {}, {}};
    }
    if (!m_socket.waitForReadyRead(m_timeout)) {
        return ModbusResult{false, ExceptionCode::None, "Read timeout", {}, {}};
    }

    QByteArray response = m_socket.readAll();
    if (response.size() < 8) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[7]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[8]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseWriteResponse(response);
}

} // namespace modbus

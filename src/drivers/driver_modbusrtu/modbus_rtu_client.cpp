#include "modbus_rtu_client.h"
#include <QDataStream>

namespace modbus {

// CRC16 查找表（Modbus 标准多项式 0xA001）
static const uint16_t crc16Table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

uint16_t ModbusRtuClient::calculateCRC16(const QByteArray& data)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ crc16Table[(crc ^ byte) & 0xFF];
    }
    return crc;
}

ModbusRtuClient::ModbusRtuClient(int timeout)
    : m_timeout(timeout)
{
}

ModbusRtuClient::~ModbusRtuClient()
{
    disconnect();
}

bool ModbusRtuClient::connectToServer(const QString& host, quint16 port)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        if (m_socket.peerAddress().toString() == host && m_socket.peerPort() == port) {
            return true;
        }
        disconnect();
    }

    m_socket.connectToHost(host, port);
    return m_socket.waitForConnected(m_timeout);
}

void ModbusRtuClient::disconnect()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(1000);
        }
    }
}

bool ModbusRtuClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool ModbusRtuClient::verifyCRC(const QByteArray& frame)
{
    if (frame.size() < 4) return false;

    QByteArray dataWithoutCrc = frame.left(frame.size() - 2);
    uint16_t calculatedCrc = calculateCRC16(dataWithoutCrc);

    uint16_t receivedCrc = static_cast<uint8_t>(frame[frame.size() - 2]) |
                          (static_cast<uint8_t>(frame[frame.size() - 1]) << 8);

    return calculatedCrc == receivedCrc;
}

QByteArray ModbusRtuClient::buildRequest(FunctionCode fc, const QByteArray& pdu)
{
    QByteArray request;
    request.append(static_cast<char>(m_unitId));
    request.append(static_cast<char>(static_cast<uint8_t>(fc)));
    request.append(pdu);

    uint16_t crc = calculateCRC16(request);
    request.append(static_cast<char>(crc & 0xFF));
    request.append(static_cast<char>((crc >> 8) & 0xFF));

    return request;
}

ModbusResult ModbusRtuClient::parseReadBitsResponse(const QByteArray& response, uint16_t count)
{
    ModbusResult result;
    result.success = true;

    // RTU 响应: [Unit ID (1)] [FC (1)] [Byte Count (1)] [Data (N)] [CRC16 (2)]
    // 最小长度: 1 + 1 + 1 + 1 + 2 = 6
    if (response.size() < 6) {
        result.success = false;
        result.errorMessage = "Response too short for bit data";
        return result;
    }

    uint8_t byteCount = static_cast<uint8_t>(response[2]);
    if (response.size() < 3 + byteCount + 2) {
        result.success = false;
        result.errorMessage = "Incomplete bit data";
        return result;
    }

    for (uint16_t i = 0; i < count; ++i) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        bool value = (response[3 + byteIndex] >> bitIndex) & 0x01;
        result.coils.append(value);
    }

    return result;
}

ModbusResult ModbusRtuClient::parseReadRegistersResponse(const QByteArray& response)
{
    ModbusResult result;
    result.success = true;

    // RTU 响应: [Unit ID (1)] [FC (1)] [Byte Count (1)] [Data (N)] [CRC16 (2)]
    if (response.size() < 6) {
        result.success = false;
        result.errorMessage = "Response too short for register data";
        return result;
    }

    uint8_t byteCount = static_cast<uint8_t>(response[2]);
    if (response.size() < 3 + byteCount + 2) {
        result.success = false;
        result.errorMessage = "Incomplete register data";
        return result;
    }

    int regCount = byteCount / 2;
    for (int i = 0; i < regCount; ++i) {
        uint16_t value = (static_cast<uint8_t>(response[3 + i*2]) << 8) |
                          static_cast<uint8_t>(response[3 + i*2 + 1]);
        result.registers.append(value);
    }

    return result;
}

ModbusResult ModbusRtuClient::parseWriteResponse(const QByteArray& response)
{
    ModbusResult result;
    // RTU 写响应: [Unit ID (1)] [FC (1)] [Address (2)] [Value/Count (2)] [CRC16 (2)] = 8 bytes
    result.success = (response.size() >= 8);
    if (!result.success) {
        result.errorMessage = "Write response too short";
    }
    return result;
}

ModbusResult ModbusRtuClient::readCoils(uint16_t address, uint16_t count)
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

    // RTU 最小响应: Unit ID + FC + CRC = 4 bytes
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseReadBitsResponse(response, count);
}

ModbusResult ModbusRtuClient::readDiscreteInputs(uint16_t address, uint16_t count)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseReadBitsResponse(response, count);
}

ModbusResult ModbusRtuClient::readHoldingRegisters(uint16_t address, uint16_t count)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseReadRegistersResponse(response);
}

ModbusResult ModbusRtuClient::readInputRegisters(uint16_t address, uint16_t count)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseReadRegistersResponse(response);
}

ModbusResult ModbusRtuClient::writeSingleCoil(uint16_t address, bool value)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseWriteResponse(response);
}

ModbusResult ModbusRtuClient::writeSingleRegister(uint16_t address, uint16_t value)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseWriteResponse(response);
}

ModbusResult ModbusRtuClient::writeMultipleCoils(uint16_t address, const QVector<bool>& values)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseWriteResponse(response);
}

ModbusResult ModbusRtuClient::writeMultipleRegisters(uint16_t address, const QVector<uint16_t>& values)
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
    if (response.size() < 4) {
        return ModbusResult{false, ExceptionCode::None, "Response too short", {}, {}};
    }

    if (!verifyCRC(response)) {
        return ModbusResult{false, ExceptionCode::None, "CRC error", {}, {}};
    }

    uint8_t fc = static_cast<uint8_t>(response[1]);
    if (fc & 0x80) {
        auto ex = static_cast<ExceptionCode>(response[2]);
        return ModbusResult{false, ex, exceptionMessage(ex), {}, {}};
    }

    return parseWriteResponse(response);
}

} // namespace modbus

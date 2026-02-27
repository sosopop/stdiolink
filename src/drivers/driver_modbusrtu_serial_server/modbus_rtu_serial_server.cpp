#include "modbus_rtu_serial_server.h"
#include <QtMath>

enum SerialFunctionCode {
    SFC_READ_COILS = 0x01,
    SFC_READ_DISCRETE_INPUTS = 0x02,
    SFC_READ_HOLDING_REGISTERS = 0x03,
    SFC_READ_INPUT_REGISTERS = 0x04,
    SFC_WRITE_SINGLE_COIL = 0x05,
    SFC_WRITE_SINGLE_REGISTER = 0x06,
    SFC_WRITE_MULTIPLE_COILS = 0x0F,
    SFC_WRITE_MULTIPLE_REGISTERS = 0x10
};

enum SerialModbusException {
    SFC_ILLEGAL_FUNCTION = 0x01,
    SFC_ILLEGAL_DATA_ADDRESS = 0x02,
    SFC_ILLEGAL_DATA_VALUE = 0x03,
    SFC_SLAVE_DEVICE_FAILURE = 0x04
};

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

uint16_t ModbusRtuSerialServer::calculateCRC16(const QByteArray& data) {
    uint16_t crc = 0xFFFF;
    for (char c : data) {
        uint8_t byte = static_cast<uint8_t>(c);
        crc = (crc >> 8) ^ crc16Table[(crc ^ byte) & 0xFF];
    }
    return crc;
}

QByteArray ModbusRtuSerialServer::buildRtuResponse(quint8 unitId, const QByteArray& pdu) {
    QByteArray frame;
    frame.append(static_cast<char>(unitId));
    frame.append(pdu);
    uint16_t crc = calculateCRC16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

double ModbusRtuSerialServer::calculateT35(int baudRate, int dataBits,
                                            bool hasParity, double stopBits) {
    if (baudRate > 19200) return 1.75;
    double bitsPerChar = 1.0 + dataBits + (hasParity ? 1 : 0) + stopBits;
    return 3.5 * bitsPerChar / baudRate * 1000.0;
}

ModbusRtuSerialServer::ModbusRtuSerialServer(QObject* parent) : QObject(parent) {
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &ModbusRtuSerialServer::onFrameTimeout);
}

ModbusRtuSerialServer::~ModbusRtuSerialServer() { stopServer(); }

bool ModbusRtuSerialServer::startServer(const QString& portName, int baudRate,
        int dataBits, const QString& stopBits, const QString& parity) {
    if (m_serial) return false;

    m_serial = new QSerialPort(this);
    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(static_cast<QSerialPort::DataBits>(dataBits));

    if (stopBits == "1.5")
        m_serial->setStopBits(QSerialPort::OneAndHalfStop);
    else if (stopBits == "2")
        m_serial->setStopBits(QSerialPort::TwoStop);
    else
        m_serial->setStopBits(QSerialPort::OneStop);

    if (parity == "even")
        m_serial->setParity(QSerialPort::EvenParity);
    else if (parity == "odd")
        m_serial->setParity(QSerialPort::OddParity);
    else
        m_serial->setParity(QSerialPort::NoParity);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        delete m_serial;
        m_serial = nullptr;
        return false;
    }

    bool hasParity = (parity != "none");
    double stopBitsVal = (stopBits == "1.5") ? 1.5 : stopBits.toDouble();
    m_t35Ms = calculateT35(baudRate, dataBits, hasParity, stopBitsVal);

    connect(m_serial, &QSerialPort::readyRead, this, &ModbusRtuSerialServer::onReadyRead);
    return true;
}

void ModbusRtuSerialServer::stopServer() {
    m_frameTimer.stop();
    m_recvBuffer.clear();
    if (m_serial) {
        m_serial->close();
        delete m_serial;
        m_serial = nullptr;
    }
}

bool ModbusRtuSerialServer::isRunning() const {
    return m_serial && m_serial->isOpen();
}

QString ModbusRtuSerialServer::portName() const {
    return m_serial ? m_serial->portName() : QString();
}

bool ModbusRtuSerialServer::addUnit(quint8 unitId, int dataAreaSize) {
    QMutexLocker locker(&m_mutex);
    if (m_unitDataAreas.contains(unitId)) return false;
    m_unitDataAreas.insert(unitId, QSharedPointer<SerialModbusDataArea>::create(dataAreaSize));
    return true;
}

bool ModbusRtuSerialServer::removeUnit(quint8 unitId) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    m_unitDataAreas.remove(unitId);
    return true;
}

bool ModbusRtuSerialServer::hasUnit(quint8 unitId) const {
    QMutexLocker locker(&m_mutex);
    return m_unitDataAreas.contains(unitId);
}

QList<quint8> ModbusRtuSerialServer::getUnits() const {
    QMutexLocker locker(&m_mutex);
    return m_unitDataAreas.keys();
}

void ModbusRtuSerialServer::onReadyRead() {
    if (!m_serial || !m_serial->isOpen()) return;
    m_recvBuffer.append(m_serial->readAll());
    if (m_recvBuffer.size() > 256) {
        m_recvBuffer.clear();
        return;
    }
    m_frameTimer.start(qMax(1, static_cast<int>(qCeil(m_t35Ms))));
}

void ModbusRtuSerialServer::onFrameTimeout() {
    if (m_recvBuffer.size() < 4) {
        m_recvBuffer.clear();
        return;
    }
    uint16_t received = (static_cast<uint8_t>(m_recvBuffer[m_recvBuffer.size()-1]) << 8)
                      | static_cast<uint8_t>(m_recvBuffer[m_recvBuffer.size()-2]);
    uint16_t calculated = calculateCRC16(m_recvBuffer.left(m_recvBuffer.size()-2));
    if (received != calculated) {
        m_recvBuffer.clear();
        return;
    }

    QByteArray response = processRtuRequest(m_recvBuffer);
    m_recvBuffer.clear();
    if (!response.isEmpty() && m_serial && m_serial->isOpen()) {
        m_serial->write(response);
        m_serial->flush();
    }
}

void ModbusRtuSerialServer::applyBroadcastWrite(quint8 fc, const QByteArray& data,
        QSharedPointer<SerialModbusDataArea> dataArea) {
    // Broadcast writes are best-effort: silently skip if address out of range
    if (fc == SFC_WRITE_SINGLE_COIL && data.size() >= 4) {
        quint16 addr = bytesToUInt16(data, 0);
        quint16 val = bytesToUInt16(data, 2);
        if (addr < dataArea->coils.size() && (val == 0x0000 || val == 0xFF00))
            dataArea->coils[addr] = (val == 0xFF00);
    }
    else if (fc == SFC_WRITE_SINGLE_REGISTER && data.size() >= 4) {
        quint16 addr = bytesToUInt16(data, 0);
        quint16 val = bytesToUInt16(data, 2);
        if (addr < dataArea->holdingRegisters.size())
            dataArea->holdingRegisters[addr] = val;
    }
    else if (fc == SFC_WRITE_MULTIPLE_COILS && data.size() >= 5) {
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        quint8 byteCount = static_cast<quint8>(data[4]);
        if (qty >= 1 && qty <= 1968 && byteCount == (qty + 7) / 8
            && data.size() >= 5 + byteCount
            && startAddr + qty <= dataArea->coils.size()) {
            for (int i = 0; i < qty; i++)
                dataArea->coils[startAddr + i] = (data[5 + i / 8] >> (i % 8)) & 0x01;
        }
    }
    else if (fc == SFC_WRITE_MULTIPLE_REGISTERS && data.size() >= 5) {
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        quint8 byteCount = static_cast<quint8>(data[4]);
        if (qty >= 1 && qty <= 123 && byteCount == qty * 2
            && data.size() >= 5 + byteCount
            && startAddr + qty <= dataArea->holdingRegisters.size()) {
            for (int i = 0; i < qty; i++)
                dataArea->holdingRegisters[startAddr + i] = bytesToUInt16(data, 5 + i * 2);
        }
    }
}

QByteArray ModbusRtuSerialServer::processRtuRequest(const QByteArray& frame) {
    if (frame.size() < 4) return QByteArray();

    quint8 unitId = static_cast<quint8>(frame[0]);
    quint8 fc = static_cast<quint8>(frame[1]);
    QByteArray data = frame.mid(2, frame.size() - 4);

    // Broadcast (Unit ID=0): apply writes to all units, no response
    if (unitId == 0) {
        if (fc == SFC_WRITE_SINGLE_COIL || fc == SFC_WRITE_SINGLE_REGISTER ||
            fc == SFC_WRITE_MULTIPLE_COILS || fc == SFC_WRITE_MULTIPLE_REGISTERS) {
            QMutexLocker locker(&m_mutex);
            for (auto it = m_unitDataAreas.begin(); it != m_unitDataAreas.end(); ++it) {
                applyBroadcastWrite(fc, data, it.value());
            }
        }
        return QByteArray();
    }

    // Unit ID mismatch: silent (RTU standard)
    QSharedPointer<SerialModbusDataArea> dataArea;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_unitDataAreas.contains(unitId))
            return QByteArray();
        dataArea = m_unitDataAreas[unitId];
    }

    QByteArray pdu;

    switch (fc) {
    case SFC_READ_COILS: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (qty < 1 || qty > 2000 || startAddr + qty > dataArea->coils.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        pdu.append(static_cast<char>(fc));
        int byteCount = (qty + 7) / 8;
        pdu.append(static_cast<char>(byteCount));
        for (int i = 0; i < byteCount; i++) {
            quint8 byte = 0;
            for (int bit = 0; bit < 8 && (i * 8 + bit) < qty; bit++) {
                if (dataArea->coils[startAddr + i * 8 + bit]) byte |= (1 << bit);
            }
            pdu.append(static_cast<char>(byte));
        }
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case SFC_READ_DISCRETE_INPUTS: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (qty < 1 || qty > 2000 || startAddr + qty > dataArea->discreteInputs.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        pdu.append(static_cast<char>(fc));
        int byteCount = (qty + 7) / 8;
        pdu.append(static_cast<char>(byteCount));
        for (int i = 0; i < byteCount; i++) {
            quint8 byte = 0;
            for (int bit = 0; bit < 8 && (i * 8 + bit) < qty; bit++) {
                if (dataArea->discreteInputs[startAddr + i * 8 + bit]) byte |= (1 << bit);
            }
            pdu.append(static_cast<char>(byte));
        }
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case SFC_READ_HOLDING_REGISTERS: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (qty < 1 || qty > 125 || startAddr + qty > dataArea->holdingRegisters.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        pdu.append(static_cast<char>(fc));
        pdu.append(static_cast<char>(qty * 2));
        for (int i = 0; i < qty; i++)
            pdu.append(uint16ToBytes(dataArea->holdingRegisters[startAddr + i]));
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case SFC_READ_INPUT_REGISTERS: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (qty < 1 || qty > 125 || startAddr + qty > dataArea->inputRegisters.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        pdu.append(static_cast<char>(fc));
        pdu.append(static_cast<char>(qty * 2));
        for (int i = 0; i < qty; i++)
            pdu.append(uint16ToBytes(dataArea->inputRegisters[startAddr + i]));
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case SFC_WRITE_SINGLE_COIL: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 addr = bytesToUInt16(data, 0);
        quint16 val = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (addr >= dataArea->coils.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        if (val != 0x0000 && val != 0xFF00)
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        dataArea->coils[addr] = (val == 0xFF00);
        locker.unlock();
        emit dataWritten(unitId, fc, addr, 1);
        pdu.append(static_cast<char>(fc));
        pdu.append(uint16ToBytes(addr));
        pdu.append(uint16ToBytes(val));
        break;
    }
    case SFC_WRITE_SINGLE_REGISTER: {
        if (data.size() < 4) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 addr = bytesToUInt16(data, 0);
        quint16 val = bytesToUInt16(data, 2);
        QMutexLocker locker(&m_mutex);
        if (addr >= dataArea->holdingRegisters.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        dataArea->holdingRegisters[addr] = val;
        locker.unlock();
        emit dataWritten(unitId, fc, addr, 1);
        pdu.append(static_cast<char>(fc));
        pdu.append(uint16ToBytes(addr));
        pdu.append(uint16ToBytes(val));
        break;
    }
    case SFC_WRITE_MULTIPLE_COILS: {
        if (data.size() < 5) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        quint8 byteCount = static_cast<quint8>(data[4]);
        if (qty < 1 || qty > 1968 || byteCount != (qty + 7) / 8 || data.size() < 5 + byteCount)
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        QMutexLocker locker(&m_mutex);
        if (startAddr + qty > dataArea->coils.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        for (int i = 0; i < qty; i++) {
            int byteIdx = i / 8;
            int bitIdx = i % 8;
            dataArea->coils[startAddr + i] = (data[5 + byteIdx] >> bitIdx) & 0x01;
        }
        locker.unlock();
        emit dataWritten(unitId, fc, startAddr, qty);
        pdu.append(static_cast<char>(fc));
        pdu.append(uint16ToBytes(startAddr));
        pdu.append(uint16ToBytes(qty));
        break;
    }
    case SFC_WRITE_MULTIPLE_REGISTERS: {
        if (data.size() < 5) return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(data, 0);
        quint16 qty = bytesToUInt16(data, 2);
        quint8 byteCount = static_cast<quint8>(data[4]);
        if (qty < 1 || qty > 123 || byteCount != qty * 2 || data.size() < 5 + byteCount)
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_VALUE);
        QMutexLocker locker(&m_mutex);
        if (startAddr + qty > dataArea->holdingRegisters.size())
            return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_DATA_ADDRESS);
        for (int i = 0; i < qty; i++)
            dataArea->holdingRegisters[startAddr + i] = bytesToUInt16(data, 5 + i * 2);
        locker.unlock();
        emit dataWritten(unitId, fc, startAddr, qty);
        pdu.append(static_cast<char>(fc));
        pdu.append(uint16ToBytes(startAddr));
        pdu.append(uint16ToBytes(qty));
        break;
    }
    default:
        return createRtuExceptionResponse(unitId, fc, SFC_ILLEGAL_FUNCTION);
    }

    return buildRtuResponse(unitId, pdu);
}

QByteArray ModbusRtuSerialServer::createRtuExceptionResponse(quint8 unitId, quint8 fc, quint8 exceptionCode) {
    QByteArray pdu;
    pdu.append(static_cast<char>(fc | 0x80));
    pdu.append(static_cast<char>(exceptionCode));
    return buildRtuResponse(unitId, pdu);
}

quint16 ModbusRtuSerialServer::bytesToUInt16(const QByteArray& data, int offset) const {
    return (static_cast<quint8>(data[offset]) << 8) | static_cast<quint8>(data[offset + 1]);
}

QByteArray ModbusRtuSerialServer::uint16ToBytes(quint16 value) const {
    QByteArray bytes;
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

bool ModbusRtuSerialServer::setCoil(quint8 unitId, quint16 address, bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->coils.size()) return false;
    da->coils[address] = value;
    return true;
}

bool ModbusRtuSerialServer::getCoil(quint8 unitId, quint16 address, bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->coils.size()) return false;
    value = da->coils[address];
    return true;
}

bool ModbusRtuSerialServer::setDiscreteInput(quint8 unitId, quint16 address, bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->discreteInputs.size()) return false;
    da->discreteInputs[address] = value;
    return true;
}

bool ModbusRtuSerialServer::getDiscreteInput(quint8 unitId, quint16 address, bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->discreteInputs.size()) return false;
    value = da->discreteInputs[address];
    return true;
}

bool ModbusRtuSerialServer::setHoldingRegister(quint8 unitId, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->holdingRegisters.size()) return false;
    da->holdingRegisters[address] = value;
    return true;
}

bool ModbusRtuSerialServer::getHoldingRegister(quint8 unitId, quint16 address, quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->holdingRegisters.size()) return false;
    value = da->holdingRegisters[address];
    return true;
}

bool ModbusRtuSerialServer::setInputRegister(quint8 unitId, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->inputRegisters.size()) return false;
    da->inputRegisters[address] = value;
    return true;
}

bool ModbusRtuSerialServer::getInputRegister(quint8 unitId, quint16 address, quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& da = m_unitDataAreas[unitId];
    if (address >= da->inputRegisters.size()) return false;
    value = da->inputRegisters[address];
    return true;
}
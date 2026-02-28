#include "modbus_tcp_server.h"

enum ModbusFunctionCode {
    READ_COILS = 0x01,
    READ_DISCRETE_INPUTS = 0x02,
    READ_HOLDING_REGISTERS = 0x03,
    READ_INPUT_REGISTERS = 0x04,
    WRITE_SINGLE_COIL = 0x05,
    WRITE_SINGLE_REGISTER = 0x06,
    WRITE_MULTIPLE_COILS = 0x0F,
    WRITE_MULTIPLE_REGISTERS = 0x10
};

enum ModbusException {
    ILLEGAL_FUNCTION = 0x01,
    ILLEGAL_DATA_ADDRESS = 0x02,
    ILLEGAL_DATA_VALUE = 0x03,
    SLAVE_DEVICE_FAILURE = 0x04,
    GATEWAY_TARGET_DEVICE_FAILED = 0x0B
};

ModbusTcpServer::ModbusTcpServer(QObject* parent) : QTcpServer(parent) {}

ModbusTcpServer::~ModbusTcpServer() { stopServer(); }

bool ModbusTcpServer::startServer(quint16 port, const QString& address) {
    if (isListening()) return false;
    QHostAddress bindAddr = address.isEmpty() ? QHostAddress::Any
                                              : QHostAddress(address);
    if (!listen(bindAddr, port)) {
        qWarning() << "Failed to start server:" << errorString();
        return false;
    }
    qInfo() << "Modbus TCP Server started on port" << serverPort();
    return true;
}

void ModbusTcpServer::stopServer() {
    if (isListening()) {
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.key()) {
                it.key()->disconnectFromHost();
                it.key()->deleteLater();
            }
        }
        m_clients.clear();
        close();
    }
}

bool ModbusTcpServer::addUnit(quint8 unitId, int dataAreaSize) {
    QMutexLocker locker(&m_mutex);
    if (m_unitDataAreas.contains(unitId)) return false;
    m_unitDataAreas.insert(unitId, QSharedPointer<ModbusDataArea>::create(dataAreaSize));
    return true;
}

bool ModbusTcpServer::removeUnit(quint8 unitId) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    m_unitDataAreas.remove(unitId);
    return true;
}

bool ModbusTcpServer::hasUnit(quint8 unitId) const {
    QMutexLocker locker(&m_mutex);
    return m_unitDataAreas.contains(unitId);
}

QList<quint8> ModbusTcpServer::getUnits() const {
    QMutexLocker locker(&m_mutex);
    return m_unitDataAreas.keys();
}

void ModbusTcpServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket* socket = new QTcpSocket(this);
    if (socket->setSocketDescriptor(socketDescriptor)) {
        QString clientAddress = socket->peerAddress().toString();
        quint16 clientPort = socket->peerPort();

        ClientInfo info;
        info.address = clientAddress;
        info.port = clientPort;
        info.recvBuffer.clear();

        QPointer<QTcpSocket> socketPtr(socket);
        m_clients.insert(socketPtr, info);

        connect(socket, &QTcpSocket::readyRead, this, &ModbusTcpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ModbusTcpServer::onDisconnected);

        emit clientConnected(clientAddress, clientPort);
    } else {
        delete socket;
    }
}

void ModbusTcpServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QPointer<QTcpSocket> socketPtr(socket);
    if (!m_clients.contains(socketPtr)) return;

    m_clients[socketPtr].recvBuffer.append(socket->readAll());
    processBuffer(socket);
}

void ModbusTcpServer::onDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QPointer<QTcpSocket> socketPtr(socket);
    if (m_clients.contains(socketPtr)) {
        ClientInfo info = m_clients.value(socketPtr);
        m_clients.remove(socketPtr);
        emit clientDisconnected(info.address, info.port);
    }
    disconnect(socket, nullptr, this, nullptr);
    socket->deleteLater();
}

void ModbusTcpServer::processBuffer(QTcpSocket* socket) {
    if (!socket) return;

    QPointer<QTcpSocket> socketPtr(socket);
    if (!m_clients.contains(socketPtr)) return;

    QByteArray& buffer = m_clients[socketPtr].recvBuffer;

    while (buffer.size() >= 7) {
        ModbusTCPHeader header;
        if (!parseHeader(buffer, header)) {
            buffer.clear();
            break;
        }
        if (header.length > MAX_MODBUS_LENGTH) {
            buffer.clear();
            break;
        }
        int frameLength = 6 + header.length;
        if (buffer.size() < frameLength) break;

        QByteArray frame = buffer.left(frameLength);
        buffer.remove(0, frameLength);

        QByteArray response = processRequest(frame);
        if (!response.isEmpty()) {
            socket->write(response);
            socket->flush();
        }
    }
}

bool ModbusTcpServer::parseHeader(const QByteArray& data, ModbusTCPHeader& header) {
    if (data.size() < 7) return false;
    header.transactionId = bytesToUInt16(data, 0);
    header.protocolId = bytesToUInt16(data, 2);
    header.length = bytesToUInt16(data, 4);
    header.unitId = static_cast<quint8>(data[6]);
    return true;
}

QByteArray ModbusTcpServer::processRequest(const QByteArray& request) {
    if (request.size() < 8) return QByteArray();

    ModbusTCPHeader header;
    if (!parseHeader(request, header)) return QByteArray();
    if (header.protocolId != 0) return QByteArray();

    QSharedPointer<ModbusDataArea> dataArea;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_unitDataAreas.contains(header.unitId)) {
            return createExceptionResponse(header, request[7], GATEWAY_TARGET_DEVICE_FAILED);
        }
        dataArea = m_unitDataAreas[header.unitId];
    }

    quint8 functionCode = static_cast<quint8>(request[7]);
    QByteArray pdu = request.mid(8);
    QByteArray responsePdu;

    switch (functionCode) {
    case READ_COILS: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        responsePdu = handleReadCoils(header, dataArea, startAddr, qty);
        break;
    }
    case READ_DISCRETE_INPUTS: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        responsePdu = handleReadDiscreteInputs(header, dataArea, startAddr, qty);
        break;
    }
    case READ_HOLDING_REGISTERS: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        responsePdu = handleReadHoldingRegisters(header, dataArea, startAddr, qty);
        break;
    }
    case READ_INPUT_REGISTERS: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        responsePdu = handleReadInputRegisters(header, dataArea, startAddr, qty);
        break;
    }
    case WRITE_SINGLE_COIL: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 addr = bytesToUInt16(pdu, 0);
        quint16 val = bytesToUInt16(pdu, 2);
        responsePdu = handleWriteSingleCoil(header, dataArea, addr, val);
        break;
    }
    case WRITE_SINGLE_REGISTER: {
        if (pdu.size() < 4) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 addr = bytesToUInt16(pdu, 0);
        quint16 val = bytesToUInt16(pdu, 2);
        responsePdu = handleWriteSingleRegister(header, dataArea, addr, val);
        break;
    }
    case WRITE_MULTIPLE_COILS: {
        if (pdu.size() < 6) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        quint8 byteCount = static_cast<quint8>(pdu[4]);
        if (byteCount != (qty + 7) / 8 || pdu.size() < 5 + byteCount)
            return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        QByteArray values = pdu.mid(5);
        responsePdu = handleWriteMultipleCoils(header, dataArea, startAddr, qty, values);
        break;
    }
    case WRITE_MULTIPLE_REGISTERS: {
        if (pdu.size() < 6) return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        quint16 startAddr = bytesToUInt16(pdu, 0);
        quint16 qty = bytesToUInt16(pdu, 2);
        quint8 byteCount = static_cast<quint8>(pdu[4]);
        if (byteCount != qty * 2 || pdu.size() < 5 + byteCount)
            return createExceptionResponse(header, functionCode, ILLEGAL_DATA_VALUE);
        QByteArray values = pdu.mid(5);
        responsePdu = handleWriteMultipleRegisters(header, dataArea, startAddr, qty, values);
        break;
    }
    default:
        return createExceptionResponse(header, functionCode, ILLEGAL_FUNCTION);
    }

    if (responsePdu.isEmpty()) return QByteArray();
    return buildResponse(header, responsePdu);
}

QByteArray ModbusTcpServer::handleReadCoils(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 2000 || startAddress + quantity > dataArea->coils.size())
        return createExceptionResponse(header, READ_COILS, ILLEGAL_DATA_ADDRESS);

    QByteArray response;
    response.append(static_cast<char>(READ_COILS));
    int byteCount = (quantity + 7) / 8;
    response.append(static_cast<char>(byteCount));
    for (int i = 0; i < byteCount; i++) {
        quint8 byte = 0;
        for (int bit = 0; bit < 8 && (i * 8 + bit) < quantity; bit++) {
            if (dataArea->coils[startAddress + i * 8 + bit]) byte |= (1 << bit);
        }
        response.append(static_cast<char>(byte));
    }
    locker.unlock();
    emit dataRead(header.unitId, READ_COILS, startAddress, quantity);
    return response;
}

QByteArray ModbusTcpServer::handleReadDiscreteInputs(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 2000 || startAddress + quantity > dataArea->discreteInputs.size())
        return createExceptionResponse(header, READ_DISCRETE_INPUTS, ILLEGAL_DATA_ADDRESS);

    QByteArray response;
    response.append(static_cast<char>(READ_DISCRETE_INPUTS));
    int byteCount = (quantity + 7) / 8;
    response.append(static_cast<char>(byteCount));
    for (int i = 0; i < byteCount; i++) {
        quint8 byte = 0;
        for (int bit = 0; bit < 8 && (i * 8 + bit) < quantity; bit++) {
            if (dataArea->discreteInputs[startAddress + i * 8 + bit]) byte |= (1 << bit);
        }
        response.append(static_cast<char>(byte));
    }
    locker.unlock();
    emit dataRead(header.unitId, READ_DISCRETE_INPUTS, startAddress, quantity);
    return response;
}

QByteArray ModbusTcpServer::handleReadHoldingRegisters(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 125 || startAddress + quantity > dataArea->holdingRegisters.size())
        return createExceptionResponse(header, READ_HOLDING_REGISTERS, ILLEGAL_DATA_ADDRESS);

    QByteArray response;
    response.append(static_cast<char>(READ_HOLDING_REGISTERS));
    response.append(static_cast<char>(quantity * 2));
    for (int i = 0; i < quantity; i++)
        response.append(uint16ToBytes(dataArea->holdingRegisters[startAddress + i]));
    locker.unlock();
    emit dataRead(header.unitId, READ_HOLDING_REGISTERS, startAddress, quantity);
    return response;
}

QByteArray ModbusTcpServer::handleReadInputRegisters(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 125 || startAddress + quantity > dataArea->inputRegisters.size())
        return createExceptionResponse(header, READ_INPUT_REGISTERS, ILLEGAL_DATA_ADDRESS);

    QByteArray response;
    response.append(static_cast<char>(READ_INPUT_REGISTERS));
    response.append(static_cast<char>(quantity * 2));
    for (int i = 0; i < quantity; i++)
        response.append(uint16ToBytes(dataArea->inputRegisters[startAddress + i]));
    locker.unlock();
    emit dataRead(header.unitId, READ_INPUT_REGISTERS, startAddress, quantity);
    return response;
}

QByteArray ModbusTcpServer::handleWriteSingleCoil(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (address >= dataArea->coils.size())
        return createExceptionResponse(header, WRITE_SINGLE_COIL, ILLEGAL_DATA_ADDRESS);
    if (value != 0x0000 && value != 0xFF00)
        return createExceptionResponse(header, WRITE_SINGLE_COIL, ILLEGAL_DATA_VALUE);
    dataArea->coils[address] = (value == 0xFF00);
    locker.unlock();
    emit dataWritten(header.unitId, WRITE_SINGLE_COIL, address, 1);

    QByteArray response;
    response.append(static_cast<char>(WRITE_SINGLE_COIL));
    response.append(uint16ToBytes(address));
    response.append(uint16ToBytes(value));
    return response;
}

QByteArray ModbusTcpServer::handleWriteSingleRegister(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (address >= dataArea->holdingRegisters.size())
        return createExceptionResponse(header, WRITE_SINGLE_REGISTER, ILLEGAL_DATA_ADDRESS);
    dataArea->holdingRegisters[address] = value;
    locker.unlock();
    emit dataWritten(header.unitId, WRITE_SINGLE_REGISTER, address, 1);

    QByteArray response;
    response.append(static_cast<char>(WRITE_SINGLE_REGISTER));
    response.append(uint16ToBytes(address));
    response.append(uint16ToBytes(value));
    return response;
}

QByteArray ModbusTcpServer::handleWriteMultipleCoils(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity,
        const QByteArray& values) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 1968 || startAddress + quantity > dataArea->coils.size())
        return createExceptionResponse(header, WRITE_MULTIPLE_COILS, ILLEGAL_DATA_ADDRESS);
    int byteCount = (quantity + 7) / 8;
    if (values.size() < byteCount)
        return createExceptionResponse(header, WRITE_MULTIPLE_COILS, ILLEGAL_DATA_VALUE);
    for (int i = 0; i < quantity; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        dataArea->coils[startAddress + i] = (values[byteIndex] & (1 << bitIndex)) != 0;
    }
    locker.unlock();
    emit dataWritten(header.unitId, WRITE_MULTIPLE_COILS, startAddress, quantity);

    QByteArray response;
    response.append(static_cast<char>(WRITE_MULTIPLE_COILS));
    response.append(uint16ToBytes(startAddress));
    response.append(uint16ToBytes(quantity));
    return response;
}

QByteArray ModbusTcpServer::handleWriteMultipleRegisters(const ModbusTCPHeader& header,
        QSharedPointer<ModbusDataArea> dataArea, quint16 startAddress, quint16 quantity,
        const QByteArray& values) {
    QMutexLocker locker(&m_mutex);
    if (quantity < 1 || quantity > 123 || startAddress + quantity > dataArea->holdingRegisters.size())
        return createExceptionResponse(header, WRITE_MULTIPLE_REGISTERS, ILLEGAL_DATA_ADDRESS);
    if (values.size() < quantity * 2)
        return createExceptionResponse(header, WRITE_MULTIPLE_REGISTERS, ILLEGAL_DATA_VALUE);
    for (int i = 0; i < quantity; i++)
        dataArea->holdingRegisters[startAddress + i] = bytesToUInt16(values, i * 2);
    locker.unlock();
    emit dataWritten(header.unitId, WRITE_MULTIPLE_REGISTERS, startAddress, quantity);

    QByteArray response;
    response.append(static_cast<char>(WRITE_MULTIPLE_REGISTERS));
    response.append(uint16ToBytes(startAddress));
    response.append(uint16ToBytes(quantity));
    return response;
}

QByteArray ModbusTcpServer::createExceptionResponse(const ModbusTCPHeader& header,
        quint8 functionCode, quint8 exceptionCode) {
    QByteArray pdu;
    pdu.append(static_cast<char>(functionCode | 0x80));
    pdu.append(static_cast<char>(exceptionCode));
    return buildResponse(header, pdu);
}

QByteArray ModbusTcpServer::buildResponse(const ModbusTCPHeader& header,
        const QByteArray& pdu) {
    QByteArray response;
    response.append(uint16ToBytes(header.transactionId));
    response.append(uint16ToBytes(header.protocolId));
    response.append(uint16ToBytes(static_cast<quint16>(pdu.size() + 1)));
    response.append(static_cast<char>(header.unitId));
    response.append(pdu);
    return response;
}

quint16 ModbusTcpServer::bytesToUInt16(const QByteArray& data, int offset) const {
    return static_cast<quint16>((static_cast<quint8>(data[offset]) << 8) |
                                 static_cast<quint8>(data[offset + 1]));
}

QByteArray ModbusTcpServer::uint16ToBytes(quint16 value) const {
    QByteArray bytes;
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

bool ModbusTcpServer::setCoil(quint8 unitId, quint16 address, bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->coils.size()) return false;
    area->coils[address] = value;
    return true;
}

bool ModbusTcpServer::getCoil(quint8 unitId, quint16 address, bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->coils.size()) return false;
    value = area->coils[address];
    return true;
}

bool ModbusTcpServer::setDiscreteInput(quint8 unitId, quint16 address, bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->discreteInputs.size()) return false;
    area->discreteInputs[address] = value;
    return true;
}

bool ModbusTcpServer::getDiscreteInput(quint8 unitId, quint16 address, bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->discreteInputs.size()) return false;
    value = area->discreteInputs[address];
    return true;
}

bool ModbusTcpServer::setHoldingRegister(quint8 unitId, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->holdingRegisters.size()) return false;
    area->holdingRegisters[address] = value;
    return true;
}

bool ModbusTcpServer::getHoldingRegister(quint8 unitId, quint16 address, quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->holdingRegisters.size()) return false;
    value = area->holdingRegisters[address];
    return true;
}

bool ModbusTcpServer::setInputRegister(quint8 unitId, quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->inputRegisters.size()) return false;
    area->inputRegisters[address] = value;
    return true;
}

bool ModbusTcpServer::getInputRegister(quint8 unitId, quint16 address, quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto& area = m_unitDataAreas[unitId];
    if (address >= area->inputRegisters.size()) return false;
    value = area->inputRegisters[address];
    return true;
}

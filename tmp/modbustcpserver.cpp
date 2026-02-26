// modbustcpserver.cpp
#include "modbustcpserver.h"
#include <cstring>

ModbusTcpServer::ModbusTcpServer(QObject* parent) : QTcpServer(parent) {
    // 默认添加unitId 1
    addUnit(1);
}

ModbusTcpServer::~ModbusTcpServer() { stopServer(); }

bool ModbusTcpServer::startServer(quint16 port) {
    if (isListening()) {
        LOGW("Server is already running");
        return false;
    }

    if (!listen(QHostAddress::Any, port)) {
        LOGE("Failed to start server: {}", errorString().toStdString());
        return false;
    }

    LOGI("Modbus TCP Server started on port {}", port);
    return true;
}

void ModbusTcpServer::stopServer() {
    if (isListening()) {
        // 关闭所有客户端连接
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.key()) {
                it.key()->disconnectFromHost();
                it.key()->deleteLater();
            }
        }
        m_clients.clear();

        close();
        LOGI("Modbus TCP Server stopped");
    }
}

bool ModbusTcpServer::addUnit(quint8 unitId, int dataAreaSize) {
    QMutexLocker locker(&m_mutex);
    if (m_unitDataAreas.contains(unitId)) {
        LOGW("Unit {} already exists", unitId);
        return false;
    }

    m_unitDataAreas.insert(
        unitId, QSharedPointer<ModbusDataArea>::create(dataAreaSize));
    LOGI("Added unit {} with data area size {}", unitId, dataAreaSize);
    return true;
}

bool ModbusTcpServer::removeUnit(quint8 unitId) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) {
        LOGW("Unit {} does not exist", unitId);
        return false;
    }

    m_unitDataAreas.remove(unitId);
    LOGI("Removed unit {}", unitId);
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

        connect(socket, &QTcpSocket::readyRead, this,
                &ModbusTcpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this,
                &ModbusTcpServer::onDisconnected);

        emit clientConnected(clientAddress, clientPort);
        LOGI("Client connected: {}:{}", clientAddress.toStdString(), clientPort);
    } else {
        delete socket;
    }
}

void ModbusTcpServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    // 检查QPointer有效性
    QPointer<QTcpSocket> socketPtr(socket);
    if (!m_clients.contains(socketPtr))
        return;

    // 读取所有可用数据并追加到缓冲区
    QByteArray data = socket->readAll();
    m_clients[socketPtr].recvBuffer.append(data);

    // 处理缓冲区中的完整帧
    processBuffer(socket);
}

void ModbusTcpServer::processBuffer(QTcpSocket* socket) {
    if (!socket)
        return;

    QPointer<QTcpSocket> socketPtr(socket);
    if (!m_clients.contains(socketPtr))
        return;

    QByteArray& buffer = m_clients[socketPtr].recvBuffer;

    // 循环处理缓冲区中的所有完整帧
    while (buffer.size() >= 7) { // MBAP头最小7字节
        // 解析MBAP头获取帧长度
        ModbusTCPHeader header;
        if (!parseHeader(buffer, header)) {
            LOGW("Invalid MBAP header, clearing buffer");
            buffer.clear();
            emit errorOccurred("Invalid MBAP header");
            break;
        }

        // 防恶意包：检查length字段上限
        if (header.length > MAX_MODBUS_LENGTH) {
            LOGW("Invalid length field: {} (max: {})", header.length, MAX_MODBUS_LENGTH);
            buffer.clear();
            emit errorOccurred(
                QString("Invalid length field: %1").arg(header.length));
            break;
        }

        // 计算完整帧长度：MBAP头(6字节) + length字段指示的长度
        int frameLength = 6 + header.length;

        // 检查是否收到完整帧
        if (buffer.size() < frameLength) {
            // 数据不完整，等待更多数据
            break;
        }

        // 提取完整帧
        QByteArray frame = buffer.left(frameLength);
        buffer.remove(0, frameLength);

        // 长度校验：header.length应该等于unitId(1) + PDU长度
        int expectedPduLength = header.length - 1; // 减去unitId字节
        int actualPduLength = frame.size() - 7;    // 减去MBAP头7字节

        if (expectedPduLength != actualPduLength) {
            qWarning() << "Length mismatch: expected PDU" << expectedPduLength
                       << "bytes, got" << actualPduLength << "bytes";
            emit errorOccurred(
                QString("Frame length mismatch for transaction %1")
                    .arg(header.transactionId));
            continue;
        }

        // 处理请求
        QByteArray response = processRequest(frame);

        if (!response.isEmpty()) {
            socket->write(response);
            socket->flush();
        }
    }
}

void ModbusTcpServer::onDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    QPointer<QTcpSocket> socketPtr(socket);
    if (m_clients.contains(socketPtr)) {
        ClientInfo info = m_clients.value(socketPtr);
        m_clients.remove(socketPtr);

        emit clientDisconnected(info.address, info.port);
        qInfo() << "Client disconnected:" << info.address;
    }

    // 断开信号连接，防止极端情况
    disconnect(socket, nullptr, this, nullptr);
    socket->deleteLater();
}

bool ModbusTcpServer::parseHeader(const QByteArray& data,
                                  ModbusTCPHeader& header) {
    if (data.size() < 7)
        return false;

    header.transactionId = bytesToUInt16(data, 0);
    header.protocolId = bytesToUInt16(data, 2);
    header.length = bytesToUInt16(data, 4);
    header.unitId = static_cast<quint8>(data[6]);

    return true;
}

QByteArray ModbusTcpServer::processRequest(const QByteArray& request) {
    if (request.size() < 8) {
        return QByteArray();
    }

    // 解析MBAP头（作为局部变量传递，避免成员变量副作用）
    ModbusTCPHeader header;
    if (!parseHeader(request, header)) {
        return QByteArray();
    }

    // 检查协议ID
    if (header.protocolId != 0) {
        LOGW("Invalid protocol ID: {}", header.protocolId);
        return QByteArray();
    }

    // 检查UnitID是否存在并获取智能指针（防止竞态条件）
    QSharedPointer<ModbusDataArea> dataArea;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_unitDataAreas.contains(header.unitId)) {
            LOGW("Unit {} not found", header.unitId);
            return createExceptionResponse(header, request[7],
                                           GATEWAY_TARGET_DEVICE_FAILED);
        }
        dataArea = m_unitDataAreas[header.unitId]; // 获取智能指针副本
    }
    // 此时即使其他线程调用removeUnit，dataArea仍然有效

    quint8 functionCode = static_cast<quint8>(request[7]);
    QByteArray pdu = request.mid(8); // PDU数据
    QByteArray responsePdu;

    emit requestReceived(header.unitId, functionCode, 0, 0);

    switch (functionCode) {
    case READ_COILS: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        responsePdu = handleReadCoils(header, dataArea, startAddress, quantity);
        break;
    }
    case READ_DISCRETE_INPUTS: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        responsePdu =
            handleReadDiscreteInputs(header, dataArea, startAddress, quantity);
        break;
    }
    case READ_HOLDING_REGISTERS: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        responsePdu = handleReadHoldingRegisters(header, dataArea, startAddress,
                                                 quantity);
        break;
    }
    case READ_INPUT_REGISTERS: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        responsePdu =
            handleReadInputRegisters(header, dataArea, startAddress, quantity);
        break;
    }
    case WRITE_SINGLE_COIL: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 address = bytesToUInt16(pdu, 0);
        quint16 value = bytesToUInt16(pdu, 2);
        responsePdu = handleWriteSingleCoil(header, dataArea, address, value);
        break;
    }
    case WRITE_SINGLE_REGISTER: {
        if (pdu.size() < 4)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 address = bytesToUInt16(pdu, 0);
        quint16 value = bytesToUInt16(pdu, 2);
        responsePdu =
            handleWriteSingleRegister(header, dataArea, address, value);
        break;
    }
    case WRITE_MULTIPLE_COILS: {
        if (pdu.size() < 6)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        QByteArray values = pdu.mid(5);
        responsePdu = handleWriteMultipleCoils(header, dataArea, startAddress,
                                               quantity, values);
        break;
    }
    case WRITE_MULTIPLE_REGISTERS: {
        if (pdu.size() < 6)
            return createExceptionResponse(header, functionCode,
                                           ILLEGAL_DATA_VALUE);
        quint16 startAddress = bytesToUInt16(pdu, 0);
        quint16 quantity = bytesToUInt16(pdu, 2);
        QByteArray values = pdu.mid(5);
        responsePdu = handleWriteMultipleRegisters(
            header, dataArea, startAddress, quantity, values);
        break;
    }
    default:
        return createExceptionResponse(header, functionCode, ILLEGAL_FUNCTION);
    }

    // 如果responsePdu为空，说明内部已经返回了异常响应
    if (responsePdu.isEmpty()) {
        return QByteArray();
    }

    return buildResponse(header, responsePdu);
}

QByteArray
ModbusTcpServer::handleReadCoils(const ModbusTCPHeader& header,
                                 QSharedPointer<ModbusDataArea> dataArea,
                                 quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 2000 ||
        startAddress + quantity > dataArea->coils.size()) {
        return createExceptionResponse(header, READ_COILS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    QByteArray response;
    response.append(static_cast<char>(READ_COILS));

    int byteCount = (quantity + 7) / 8;
    response.append(static_cast<char>(byteCount));

    for (int i = 0; i < byteCount; i++) {
        quint8 byte = 0;
        for (int bit = 0; bit < 8 && (i * 8 + bit) < quantity; bit++) {
            if (dataArea->coils[startAddress + i * 8 + bit]) {
                byte |= (1 << bit);
            }
        }
        response.append(static_cast<char>(byte));
    }

    return response;
}

QByteArray ModbusTcpServer::handleReadDiscreteInputs(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 2000 ||
        startAddress + quantity > dataArea->discreteInputs.size()) {
        return createExceptionResponse(header, READ_DISCRETE_INPUTS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    QByteArray response;
    response.append(static_cast<char>(READ_DISCRETE_INPUTS));

    int byteCount = (quantity + 7) / 8;
    response.append(static_cast<char>(byteCount));

    for (int i = 0; i < byteCount; i++) {
        quint8 byte = 0;
        for (int bit = 0; bit < 8 && (i * 8 + bit) < quantity; bit++) {
            if (dataArea->discreteInputs[startAddress + i * 8 + bit]) {
                byte |= (1 << bit);
            }
        }
        response.append(static_cast<char>(byte));
    }

    return response;
}

QByteArray ModbusTcpServer::handleReadHoldingRegisters(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 125 ||
        startAddress + quantity > dataArea->holdingRegisters.size()) {
        return createExceptionResponse(header, READ_HOLDING_REGISTERS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    QByteArray response;
    response.append(static_cast<char>(READ_HOLDING_REGISTERS));
    response.append(static_cast<char>(quantity * 2)); // Byte count

    for (int i = 0; i < quantity; i++) {
        response.append(
            uint16ToBytes(dataArea->holdingRegisters[startAddress + i]));
    }

    return response;
}

QByteArray ModbusTcpServer::handleReadInputRegisters(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 startAddress, quint16 quantity) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 125 ||
        startAddress + quantity > dataArea->inputRegisters.size()) {
        return createExceptionResponse(header, READ_INPUT_REGISTERS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    QByteArray response;
    response.append(static_cast<char>(READ_INPUT_REGISTERS));
    response.append(static_cast<char>(quantity * 2)); // Byte count

    for (int i = 0; i < quantity; i++) {
        response.append(
            uint16ToBytes(dataArea->inputRegisters[startAddress + i]));
    }

    return response;
}

QByteArray
ModbusTcpServer::handleWriteSingleCoil(const ModbusTCPHeader& header,
                                       QSharedPointer<ModbusDataArea> dataArea,
                                       quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);

    if (address >= dataArea->coils.size()) {
        return createExceptionResponse(header, WRITE_SINGLE_COIL,
                                       ILLEGAL_DATA_ADDRESS);
    }

    if (value != 0x0000 && value != 0xFF00) {
        return createExceptionResponse(header, WRITE_SINGLE_COIL,
                                       ILLEGAL_DATA_VALUE);
    }

    dataArea->coils[address] = (value == 0xFF00);

    locker.unlock();
    emit dataWritten(header.unitId, WRITE_SINGLE_COIL, address, 1);

    QByteArray response;
    response.append(static_cast<char>(WRITE_SINGLE_COIL));
    response.append(uint16ToBytes(address));
    response.append(uint16ToBytes(value));

    return response;
}

QByteArray ModbusTcpServer::handleWriteSingleRegister(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 address, quint16 value) {
    QMutexLocker locker(&m_mutex);

    if (address >= dataArea->holdingRegisters.size()) {
        return createExceptionResponse(header, WRITE_SINGLE_REGISTER,
                                       ILLEGAL_DATA_ADDRESS);
    }

    dataArea->holdingRegisters[address] = value;

    locker.unlock();
    emit dataWritten(header.unitId, WRITE_SINGLE_REGISTER, address, 1);

    QByteArray response;
    response.append(static_cast<char>(WRITE_SINGLE_REGISTER));
    response.append(uint16ToBytes(address));
    response.append(uint16ToBytes(value));

    return response;
}

QByteArray ModbusTcpServer::handleWriteMultipleCoils(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 startAddress, quint16 quantity, const QByteArray& values) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 1968 ||
        startAddress + quantity > dataArea->coils.size()) {
        return createExceptionResponse(header, WRITE_MULTIPLE_COILS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    int byteCount = (quantity + 7) / 8;
    if (values.size() < byteCount) {
        return createExceptionResponse(header, WRITE_MULTIPLE_COILS,
                                       ILLEGAL_DATA_VALUE);
    }

    for (int i = 0; i < quantity; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        bool value = (values[byteIndex] & (1 << bitIndex)) != 0;
        dataArea->coils[startAddress + i] = value;
    }

    locker.unlock();
    emit dataWritten(header.unitId, WRITE_MULTIPLE_COILS, startAddress,
                     quantity);

    QByteArray response;
    response.append(static_cast<char>(WRITE_MULTIPLE_COILS));
    response.append(uint16ToBytes(startAddress));
    response.append(uint16ToBytes(quantity));

    return response;
}

QByteArray ModbusTcpServer::handleWriteMultipleRegisters(
    const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
    quint16 startAddress, quint16 quantity, const QByteArray& values) {
    QMutexLocker locker(&m_mutex);

    if (quantity < 1 || quantity > 123 ||
        startAddress + quantity > dataArea->holdingRegisters.size()) {
        return createExceptionResponse(header, WRITE_MULTIPLE_REGISTERS,
                                       ILLEGAL_DATA_ADDRESS);
    }

    if (values.size() < quantity * 2) {
        return createExceptionResponse(header, WRITE_MULTIPLE_REGISTERS,
                                       ILLEGAL_DATA_VALUE);
    }

    for (int i = 0; i < quantity; i++) {
        quint16 value = bytesToUInt16(values, i * 2);
        dataArea->holdingRegisters[startAddress + i] = value;
    }

    locker.unlock();
    emit dataWritten(header.unitId, WRITE_MULTIPLE_REGISTERS, startAddress,
                     quantity);

    QByteArray response;
    response.append(static_cast<char>(WRITE_MULTIPLE_REGISTERS));
    response.append(uint16ToBytes(startAddress));
    response.append(uint16ToBytes(quantity));

    return response;
}

QByteArray ModbusTcpServer::createExceptionResponse(
    const ModbusTCPHeader& header, quint8 functionCode, quint8 exceptionCode) {
    QByteArray pdu;
    pdu.append(static_cast<char>(functionCode | 0x80)); // 设置最高位表示异常
    pdu.append(static_cast<char>(exceptionCode));

    return buildResponse(header, pdu);
}

QByteArray ModbusTcpServer::buildResponse(const ModbusTCPHeader& header,
                                          const QByteArray& pdu) {
    QByteArray response;
    response.append(uint16ToBytes(header.transactionId));
    response.append(uint16ToBytes(0));              // Protocol ID
    response.append(uint16ToBytes(pdu.size() + 1)); // Length = PDU + unitId
    response.append(static_cast<char>(header.unitId));
    response.append(pdu);

    return response;
}

quint16 ModbusTcpServer::bytesToUInt16(const QByteArray& data,
                                       int offset) const {
    if (offset + 1 >= data.size())
        return 0;
    return (static_cast<quint8>(data[offset]) << 8) |
           static_cast<quint8>(data[offset + 1]);
}

QByteArray ModbusTcpServer::uint16ToBytes(quint16 value) const {
    QByteArray bytes;
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

// 数据访问接口实现
bool ModbusTcpServer::setCoil(quint8 unitId, quint16 address, bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->coils.size())
        return false;
    dataArea->coils[address] = value;
    return true;
}

bool ModbusTcpServer::getCoil(quint8 unitId, quint16 address, bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->coils.size())
        return false;
    value = dataArea->coils[address];
    return true;
}

bool ModbusTcpServer::setDiscreteInput(quint8 unitId, quint16 address,
                                       bool value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->discreteInputs.size())
        return false;
    dataArea->discreteInputs[address] = value;
    return true;
}

bool ModbusTcpServer::getDiscreteInput(quint8 unitId, quint16 address,
                                       bool& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->discreteInputs.size())
        return false;
    value = dataArea->discreteInputs[address];
    return true;
}

bool ModbusTcpServer::setHoldingRegister(quint8 unitId, quint16 address,
                                         quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->holdingRegisters.size())
        return false;
    dataArea->holdingRegisters[address] = value;
    return true;
}

bool ModbusTcpServer::getHoldingRegister(quint8 unitId, quint16 address,
                                         quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->holdingRegisters.size())
        return false;
    value = dataArea->holdingRegisters[address];
    return true;
}

bool ModbusTcpServer::setInputRegister(quint8 unitId, quint16 address,
                                       quint16 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->inputRegisters.size())
        return false;
    dataArea->inputRegisters[address] = value;
    return true;
}

bool ModbusTcpServer::getInputRegister(quint8 unitId, quint16 address,
                                       quint16& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId))
        return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address >= dataArea->inputRegisters.size())
        return false;
    value = dataArea->inputRegisters[address];
    return true;
}

// 32-bit Holding Register Implementations

bool ModbusTcpServer::setHoldingRegisterInt32(quint8 unitId, quint16 address, qint32 value) {
    return setHoldingRegisterUInt32(unitId, address, static_cast<quint32>(value));
}

bool ModbusTcpServer::getHoldingRegisterInt32(quint8 unitId, quint16 address, qint32& value) {
    quint32 temp;
    if (!getHoldingRegisterUInt32(unitId, address, temp)) return false;
    value = static_cast<qint32>(temp);
    return true;
}

bool ModbusTcpServer::setHoldingRegisterUInt32(quint8 unitId, quint16 address, quint32 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address + 1 >= dataArea->holdingRegisters.size()) return false;
    
    // Big Endian: High Word first
    dataArea->holdingRegisters[address] = (value >> 16) & 0xFFFF;
    dataArea->holdingRegisters[address + 1] = value & 0xFFFF;
    return true;
}

bool ModbusTcpServer::getHoldingRegisterUInt32(quint8 unitId, quint16 address, quint32& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address + 1 >= dataArea->holdingRegisters.size()) return false;

    // Big Endian: High Word first
    quint16 high = dataArea->holdingRegisters[address];
    quint16 low = dataArea->holdingRegisters[address + 1];
    value = (static_cast<quint32>(high) << 16) | low;
    return true;
}

bool ModbusTcpServer::setHoldingRegisterFloat(quint8 unitId, quint16 address, float value) {
    quint32 temp;
    std::memcpy(&temp, &value, sizeof(float));
    return setHoldingRegisterUInt32(unitId, address, temp);
}

bool ModbusTcpServer::getHoldingRegisterFloat(quint8 unitId, quint16 address, float& value) {
    quint32 temp;
    if (!getHoldingRegisterUInt32(unitId, address, temp)) return false;
    std::memcpy(&value, &temp, sizeof(float));
    return true;
}

// 32-bit Input Register Implementations

bool ModbusTcpServer::setInputRegisterInt32(quint8 unitId, quint16 address, qint32 value) {
    return setInputRegisterUInt32(unitId, address, static_cast<quint32>(value));
}

bool ModbusTcpServer::getInputRegisterInt32(quint8 unitId, quint16 address, qint32& value) {
    quint32 temp;
    if (!getInputRegisterUInt32(unitId, address, temp)) return false;
    value = static_cast<qint32>(temp);
    return true;
}

bool ModbusTcpServer::setInputRegisterUInt32(quint8 unitId, quint16 address, quint32 value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address + 1 >= dataArea->inputRegisters.size()) return false;
    
    // Big Endian: High Word first
    dataArea->inputRegisters[address] = (value >> 16) & 0xFFFF;
    dataArea->inputRegisters[address + 1] = value & 0xFFFF;
    return true;
}

bool ModbusTcpServer::getInputRegisterUInt32(quint8 unitId, quint16 address, quint32& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_unitDataAreas.contains(unitId)) return false;
    auto dataArea = m_unitDataAreas[unitId];
    if (address + 1 >= dataArea->inputRegisters.size()) return false;

    // Big Endian: High Word first
    quint16 high = dataArea->inputRegisters[address];
    quint16 low = dataArea->inputRegisters[address + 1];
    value = (static_cast<quint32>(high) << 16) | low;
    return true;
}

bool ModbusTcpServer::setInputRegisterFloat(quint8 unitId, quint16 address, float value) {
    quint32 temp;
    std::memcpy(&temp, &value, sizeof(float));
    return setInputRegisterUInt32(unitId, address, temp);
}

bool ModbusTcpServer::getInputRegisterFloat(quint8 unitId, quint16 address, float& value) {
    quint32 temp;
    if (!getInputRegisterUInt32(unitId, address, temp)) return false;
    std::memcpy(&value, &temp, sizeof(float));
    return true;
}
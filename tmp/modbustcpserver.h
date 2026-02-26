// modbustcpserver.h
#ifndef MODBUSTCPSERVER_H
#define MODBUSTCPSERVER_H

#include <QDebug>
#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>
#include "common/applog.h"

// Modbus功能码
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

// Modbus异常码
enum ModbusException {
    ILLEGAL_FUNCTION = 0x01,
    ILLEGAL_DATA_ADDRESS = 0x02,
    ILLEGAL_DATA_VALUE = 0x03,
    SLAVE_DEVICE_FAILURE = 0x04,
    GATEWAY_TARGET_DEVICE_FAILED = 0x0B
};

// Modbus TCP数据结构
struct ModbusTCPHeader {
    quint16 transactionId;
    quint16 protocolId;
    quint16 length;
    quint8 unitId;
};

// 数据存储区
struct ModbusDataArea {
    QVector<bool> coils;               // 线圈 (0x)
    QVector<bool> discreteInputs;      // 离散输入 (1x)
    QVector<quint16> holdingRegisters; // 保持寄存器 (4x)
    QVector<quint16> inputRegisters;   // 输入寄存器 (3x)

    // 支持自定义大小
    explicit ModbusDataArea(int size = 10000) {
        coils = QVector<bool>(size, false);
        discreteInputs = QVector<bool>(size, false);
        holdingRegisters = QVector<quint16>(size, 0);
        inputRegisters = QVector<quint16>(size, 0);
    }
};

// 客户端连接信息
struct ClientInfo {
    QByteArray recvBuffer; // 接收缓冲区（解决粘包问题）
    QString address;
    quint16 port;
};

class ModbusTcpServer : public QTcpServer {
    Q_OBJECT

  public:
    explicit ModbusTcpServer(QObject* parent = nullptr);
    ~ModbusTcpServer();

    // 启动服务器
    bool startServer(quint16 port = 502);

    // 停止服务器
    void stopServer();

    // UnitID管理 (支持自定义数据区大小)
    bool addUnit(quint8 unitId, int dataAreaSize = 10000);
    bool removeUnit(quint8 unitId);
    bool hasUnit(quint8 unitId) const;
    QList<quint8> getUnits() const;

    // 数据访问接口（需要指定unitId）
    bool setCoil(quint8 unitId, quint16 address, bool value);
    bool getCoil(quint8 unitId, quint16 address, bool& value);

    bool setDiscreteInput(quint8 unitId, quint16 address, bool value);
    bool getDiscreteInput(quint8 unitId, quint16 address, bool& value);

    bool setHoldingRegister(quint8 unitId, quint16 address, quint16 value);
    bool getHoldingRegister(quint8 unitId, quint16 address, quint16& value);

    // 32-bit access for Holding Registers
    bool setHoldingRegisterInt32(quint8 unitId, quint16 address, qint32 value);
    bool getHoldingRegisterInt32(quint8 unitId, quint16 address, qint32& value);
    bool setHoldingRegisterUInt32(quint8 unitId, quint16 address, quint32 value);
    bool getHoldingRegisterUInt32(quint8 unitId, quint16 address, quint32& value);
    bool setHoldingRegisterFloat(quint8 unitId, quint16 address, float value);
    bool getHoldingRegisterFloat(quint8 unitId, quint16 address, float& value);

    bool setInputRegister(quint8 unitId, quint16 address, quint16 value);
    bool getInputRegister(quint8 unitId, quint16 address, quint16& value);

    // 32-bit access for Input Registers
    bool setInputRegisterInt32(quint8 unitId, quint16 address, qint32 value);
    bool getInputRegisterInt32(quint8 unitId, quint16 address, qint32& value);
    bool setInputRegisterUInt32(quint8 unitId, quint16 address, quint32 value);
    bool getInputRegisterUInt32(quint8 unitId, quint16 address, quint32& value);
    bool setInputRegisterFloat(quint8 unitId, quint16 address, float value);
    bool getInputRegisterFloat(quint8 unitId, quint16 address, float& value);

  signals:
    void clientConnected(QString address, quint16 port);
    void clientDisconnected(QString address, quint16 port);
    void requestReceived(quint8 unitId, quint8 functionCode, quint16 address,
                         quint16 quantity);
    void dataWritten(quint8 unitId, quint8 functionCode, quint16 address,
                     quint16 quantity);
    void errorOccurred(QString error);

  protected:
    void incomingConnection(qintptr socketDescriptor) override;

  private slots:
    void onReadyRead();
    void onDisconnected();

  private:
    // 协议解析
    bool parseHeader(const QByteArray& data, ModbusTCPHeader& header);
    QByteArray processRequest(const QByteArray& request);

    // 处理粘包：从缓冲区提取完整帧
    void processBuffer(QTcpSocket* socket);

    // 功能码处理（传递header和dataArea智能指针）
    QByteArray handleReadCoils(const ModbusTCPHeader& header,
                               QSharedPointer<ModbusDataArea> dataArea,
                               quint16 startAddress, quint16 quantity);
    QByteArray handleReadDiscreteInputs(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity);
    QByteArray
    handleReadHoldingRegisters(const ModbusTCPHeader& header,
                               QSharedPointer<ModbusDataArea> dataArea,
                               quint16 startAddress, quint16 quantity);
    QByteArray handleReadInputRegisters(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity);
    QByteArray handleWriteSingleCoil(const ModbusTCPHeader& header,
                                     QSharedPointer<ModbusDataArea> dataArea,
                                     quint16 address, quint16 value);
    QByteArray
    handleWriteSingleRegister(const ModbusTCPHeader& header,
                              QSharedPointer<ModbusDataArea> dataArea,
                              quint16 address, quint16 value);
    QByteArray handleWriteMultipleCoils(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity,
                                        const QByteArray& values);
    QByteArray handleWriteMultipleRegisters(
        const ModbusTCPHeader& header, QSharedPointer<ModbusDataArea> dataArea,
        quint16 startAddress, quint16 quantity, const QByteArray& values);

    // 异常响应
    QByteArray createExceptionResponse(const ModbusTCPHeader& header,
                                       quint8 functionCode,
                                       quint8 exceptionCode);

    // 构造完整响应（MBAP + PDU）
    QByteArray buildResponse(const ModbusTCPHeader& header,
                             const QByteArray& pdu);

    // 工具函数
    quint16 bytesToUInt16(const QByteArray& data, int offset) const;
    QByteArray uint16ToBytes(quint16 value) const;

    // 数据成员
    QMap<QPointer<QTcpSocket>, ClientInfo>
        m_clients; // 使用QPointer防止悬挂指针
    QMap<quint8, QSharedPointer<ModbusDataArea>>
        m_unitDataAreas;    // 使用智能指针管理数据区
    mutable QMutex m_mutex; // mutable允许在const函数中使用

    static constexpr int MAX_MODBUS_LENGTH = 260; // Modbus TCP最大长度限制
};

#endif // MODBUSTCPSERVER_H
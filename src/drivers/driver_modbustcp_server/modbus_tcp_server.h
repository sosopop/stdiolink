#pragma once

#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>

struct ModbusTCPHeader {
    quint16 transactionId;
    quint16 protocolId;
    quint16 length;
    quint8 unitId;
};

struct ModbusDataArea {
    QVector<bool> coils;
    QVector<bool> discreteInputs;
    QVector<quint16> holdingRegisters;
    QVector<quint16> inputRegisters;

    explicit ModbusDataArea(int size = 10000) {
        coils = QVector<bool>(size, false);
        discreteInputs = QVector<bool>(size, false);
        holdingRegisters = QVector<quint16>(size, 0);
        inputRegisters = QVector<quint16>(size, 0);
    }
};

struct ClientInfo {
    QByteArray recvBuffer;
    QString address;
    quint16 port;
};

class ModbusTcpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit ModbusTcpServer(QObject* parent = nullptr);
    ~ModbusTcpServer();

    bool startServer(quint16 port = 502);
    void stopServer();
    bool isRunning() const { return isListening(); }
    quint16 serverPort() const { return isListening() ? QTcpServer::serverPort() : 0; }

    bool addUnit(quint8 unitId, int dataAreaSize = 10000);
    bool removeUnit(quint8 unitId);
    bool hasUnit(quint8 unitId) const;
    QList<quint8> getUnits() const;

    bool setCoil(quint8 unitId, quint16 address, bool value);
    bool getCoil(quint8 unitId, quint16 address, bool& value);
    bool setDiscreteInput(quint8 unitId, quint16 address, bool value);
    bool getDiscreteInput(quint8 unitId, quint16 address, bool& value);
    bool setHoldingRegister(quint8 unitId, quint16 address, quint16 value);
    bool getHoldingRegister(quint8 unitId, quint16 address, quint16& value);
    bool setInputRegister(quint8 unitId, quint16 address, quint16 value);
    bool getInputRegister(quint8 unitId, quint16 address, quint16& value);

signals:
    void clientConnected(QString address, quint16 port);
    void clientDisconnected(QString address, quint16 port);
    void dataWritten(quint8 unitId, quint8 functionCode,
                     quint16 address, quint16 quantity);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    bool parseHeader(const QByteArray& data, ModbusTCPHeader& header);
    QByteArray processRequest(const QByteArray& request);
    void processBuffer(QTcpSocket* socket);

    QByteArray handleReadCoils(const ModbusTCPHeader& header,
                               QSharedPointer<ModbusDataArea> dataArea,
                               quint16 startAddress, quint16 quantity);
    QByteArray handleReadDiscreteInputs(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity);
    QByteArray handleReadHoldingRegisters(const ModbusTCPHeader& header,
                                          QSharedPointer<ModbusDataArea> dataArea,
                                          quint16 startAddress, quint16 quantity);
    QByteArray handleReadInputRegisters(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity);
    QByteArray handleWriteSingleCoil(const ModbusTCPHeader& header,
                                     QSharedPointer<ModbusDataArea> dataArea,
                                     quint16 address, quint16 value);
    QByteArray handleWriteSingleRegister(const ModbusTCPHeader& header,
                                         QSharedPointer<ModbusDataArea> dataArea,
                                         quint16 address, quint16 value);
    QByteArray handleWriteMultipleCoils(const ModbusTCPHeader& header,
                                        QSharedPointer<ModbusDataArea> dataArea,
                                        quint16 startAddress, quint16 quantity,
                                        const QByteArray& values);
    QByteArray handleWriteMultipleRegisters(const ModbusTCPHeader& header,
                                            QSharedPointer<ModbusDataArea> dataArea,
                                            quint16 startAddress, quint16 quantity,
                                            const QByteArray& values);

    QByteArray createExceptionResponse(const ModbusTCPHeader& header,
                                       quint8 functionCode, quint8 exceptionCode);
    QByteArray buildResponse(const ModbusTCPHeader& header, const QByteArray& pdu);

    quint16 bytesToUInt16(const QByteArray& data, int offset) const;
    QByteArray uint16ToBytes(quint16 value) const;

    QMap<QPointer<QTcpSocket>, ClientInfo> m_clients;
    QMap<quint8, QSharedPointer<ModbusDataArea>> m_unitDataAreas;
    mutable QMutex m_mutex;

    static constexpr int MAX_MODBUS_LENGTH = 260;
};

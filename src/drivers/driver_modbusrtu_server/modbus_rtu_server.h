#pragma once

#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

struct ModbusDataArea_Rtu {
    QVector<bool> coils;
    QVector<bool> discreteInputs;
    QVector<quint16> holdingRegisters;
    QVector<quint16> inputRegisters;

    explicit ModbusDataArea_Rtu(int size = 10000) {
        coils = QVector<bool>(size, false);
        discreteInputs = QVector<bool>(size, false);
        holdingRegisters = QVector<quint16>(size, 0);
        inputRegisters = QVector<quint16>(size, 0);
    }
};

struct RtuClientInfo {
    QByteArray recvBuffer;
    QTimer* frameTimer;
    QString address;
    quint16 port;
};

class ModbusRtuServer : public QTcpServer {
    Q_OBJECT

public:
    explicit ModbusRtuServer(QObject* parent = nullptr);
    ~ModbusRtuServer();

    bool startServer(quint16 port = 502, const QString& address = QString());
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

    static uint16_t calculateCRC16(const QByteArray& data);
    static QByteArray buildRtuResponse(quint8 unitId, const QByteArray& pdu);

signals:
    void clientConnected(QString address, quint16 port);
    void clientDisconnected(QString address, quint16 port);
    void dataWritten(quint8 unitId, quint8 functionCode,
                     quint16 address, quint16 quantity);
    void dataRead(quint8 unitId, quint8 functionCode,
                  quint16 address, quint16 quantity);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void onFrameTimeout(QTcpSocket* socket);
    QByteArray processRtuRequest(const QByteArray& frame);
    QByteArray createRtuExceptionResponse(quint8 unitId, quint8 fc, quint8 exceptionCode);

    quint16 bytesToUInt16(const QByteArray& data, int offset) const;
    QByteArray uint16ToBytes(quint16 value) const;

    QMap<QPointer<QTcpSocket>, RtuClientInfo> m_clients;
    QMap<quint8, QSharedPointer<ModbusDataArea_Rtu>> m_unitDataAreas;
    mutable QMutex m_mutex;

    static constexpr int FRAME_TIMEOUT_MS = 50;
    static constexpr int MAX_RECV_BUFFER = 4096;
};

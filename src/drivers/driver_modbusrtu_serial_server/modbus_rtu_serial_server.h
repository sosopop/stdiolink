#pragma once

#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSerialPort>
#include <QSharedPointer>
#include <QTimer>
#include <QVector>

struct SerialModbusDataArea {
    QVector<bool> coils;
    QVector<bool> discreteInputs;
    QVector<quint16> holdingRegisters;
    QVector<quint16> inputRegisters;

    explicit SerialModbusDataArea(int size = 10000) {
        coils = QVector<bool>(size, false);
        discreteInputs = QVector<bool>(size, false);
        holdingRegisters = QVector<quint16>(size, 0);
        inputRegisters = QVector<quint16>(size, 0);
    }
};

class ModbusRtuSerialServer : public QObject {
    Q_OBJECT

public:
    explicit ModbusRtuSerialServer(QObject* parent = nullptr);
    ~ModbusRtuSerialServer();

    bool startServer(const QString& portName, int baudRate = 9600,
                     int dataBits = 8, const QString& stopBits = "1",
                     const QString& parity = "none");
    void stopServer();
    bool isRunning() const;
    QString portName() const;

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
    static double calculateT35(int baudRate, int dataBits,
                               bool hasParity, double stopBits);

signals:
    void dataWritten(quint8 unitId, quint8 functionCode,
                     quint16 address, quint16 quantity);
    void dataRead(quint8 unitId, quint8 functionCode,
                  quint16 address, quint16 quantity);

private slots:
    void onReadyRead();
    void onFrameTimeout();

private:
    QByteArray processRtuRequest(const QByteArray& frame);
    void applyBroadcastWrite(quint8 fc, const QByteArray& data,
                             QSharedPointer<SerialModbusDataArea> dataArea);
    QByteArray createRtuExceptionResponse(quint8 unitId, quint8 fc, quint8 exceptionCode);
    quint16 bytesToUInt16(const QByteArray& data, int offset) const;
    QByteArray uint16ToBytes(quint16 value) const;

    QSerialPort* m_serial = nullptr;
    QTimer m_frameTimer;
    QByteArray m_recvBuffer;
    double m_t35Ms = 3.646;
    QMap<quint8, QSharedPointer<SerialModbusDataArea>> m_unitDataAreas;
    mutable QMutex m_mutex;
};

#pragma once

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <vector>

#include "stdiolink/platform/platform_utils.h"

class PlcCraneSimHandle {
public:
    PlcCraneSimHandle() = default;

    PlcCraneSimHandle(const QString& exePath,
                      quint16 port,
                      quint8 unitId,
                      const QProcessEnvironment& env)
        : m_exePath(exePath)
        , m_port(port)
        , m_unitId(unitId)
        , m_env(env) {
    }

    PlcCraneSimHandle(const PlcCraneSimHandle&) = delete;
    PlcCraneSimHandle& operator=(const PlcCraneSimHandle&) = delete;

    PlcCraneSimHandle(PlcCraneSimHandle&&) = delete;
    PlcCraneSimHandle& operator=(PlcCraneSimHandle&&) = delete;

    ~PlcCraneSimHandle() {
        stop();
    }

    static QString findExecutable() {
        const QString driverName = "stdio.drv.plc_crane_sim";
        const QString binDir = QCoreApplication::applicationDirPath();
        const QStringList candidates{
            QDir(binDir + "/..").absoluteFilePath("data_root/drivers/" + driverName),
            QDir(binDir).absoluteFilePath("../runtime_debug/data_root/drivers/" + driverName),
            QDir(binDir).absoluteFilePath("../runtime_release/data_root/drivers/" + driverName),
        };
        for (const QString& driverDir : candidates) {
            const QString path = stdiolink::PlatformUtils::executablePath(driverDir, driverName);
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
        return {};
    }

    static QProcessEnvironment childProcessEnv() {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList extraDirs{
            appDir,
            QDir(appDir).absoluteFilePath("../runtime_release/bin"),
            QDir(appDir).absoluteFilePath("../runtime_debug/bin"),
        };
        QStringList existingDirs;
        for (const QString& dir : extraDirs) {
            if (QFileInfo::exists(dir)) {
                existingDirs.append(dir);
            }
        }
        const QString oldPath = env.value("PATH");
        const QString extraPath = existingDirs.join(QDir::listSeparator());
        env.insert("PATH", oldPath.isEmpty() ? extraPath : (extraPath + QDir::listSeparator() + oldPath));
        return env;
    }

    static PlcCraneSimHandle create(quint8 unitId = 1) {
        return PlcCraneSimHandle(findExecutable(), allocateLocalPort(), unitId, childProcessEnv());
    }

    bool start(int cylinderUpDelayMs = 40,
               int cylinderDownDelayMs = 40,
               int valveOpenDelayMs = 40,
               int valveCloseDelayMs = 40,
               int heartbeatMs = 0) {
        if (m_exePath.isEmpty() || m_port == 0) {
            m_error = "invalid plc_crane_sim configuration";
            return false;
        }

        m_proc.setProgram(m_exePath);
        m_proc.setArguments({
            "--profile=oneshot",
            "--mode=console",
            "--cmd=run",
            "--listen_address=127.0.0.1",
            "--listen_port=" + QString::number(m_port),
            "--unit_id=" + QString::number(m_unitId),
            "--event_mode=none",
            "--cylinder_up_delay=" + QString::number(cylinderUpDelayMs),
            "--cylinder_down_delay=" + QString::number(cylinderDownDelayMs),
            "--valve_open_delay=" + QString::number(valveOpenDelayMs),
            "--valve_close_delay=" + QString::number(valveCloseDelayMs),
            "--heartbeat_ms=" + QString::number(heartbeatMs),
        });
        m_proc.setProcessEnvironment(m_env);
        m_proc.setProcessChannelMode(QProcess::SeparateChannels);
        m_proc.start();
        if (!m_proc.waitForStarted(3000)) {
            m_error = "failed to start plc_crane_sim: " + m_proc.errorString();
            return false;
        }

        const qint64 deadlineMs = 4000;
        qint64 waitedMs = 0;
        while (waitedMs < deadlineMs) {
            if (m_proc.state() == QProcess::NotRunning) {
                m_error = QString("plc_crane_sim exited early, exitCode=%1, stderr=%2")
                              .arg(m_proc.exitCode())
                              .arg(QString::fromUtf8(m_proc.readAllStandardError()));
                return false;
            }
            if (canConnect()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(20);
            waitedMs += 20;
        }

        m_error = "plc_crane_sim port not reachable";
        return false;
    }

    void stop() {
        if (m_proc.state() == QProcess::NotRunning) {
            return;
        }
        m_proc.terminate();
        if (!m_proc.waitForFinished(1500)) {
            m_proc.kill();
            m_proc.waitForFinished(3000);
        }
    }

    QString host() const { return "127.0.0.1"; }
    quint16 port() const { return m_port; }
    quint8 unitId() const { return m_unitId; }
    QString error() const { return m_error; }

    bool isManualMode() const {
        QString err;
        const auto regs = readHoldingRegisters(3, 1, err);
        return err.isEmpty() && regs.size() == 1 && regs[0] == 0;
    }

private:
    static quint16 allocateLocalPort() {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            return 0;
        }
        return server.serverPort();
    }

    bool canConnect() const {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, m_port);
        const bool ok = socket.waitForConnected(200);
        if (ok) {
            socket.disconnectFromHost();
        }
        return ok;
    }

    static bool readExact(QTcpSocket& socket, int size, QByteArray& out, QString& error) {
        out.clear();
        while (out.size() < size) {
            if (socket.bytesAvailable() <= 0 && !socket.waitForReadyRead(1000)) {
                error = QString("read timeout: need %1 bytes, got %2").arg(size).arg(out.size());
                return false;
            }
            out.append(socket.read(size - out.size()));
        }
        return true;
    }

    bool modbusRequest(quint8 functionCode,
                       const QByteArray& pdu,
                       quint8& responseFunction,
                       QByteArray& responseData,
                       QString& error) const {
        QTcpSocket socket;
        socket.connectToHost(host(), m_port);
        if (!socket.waitForConnected(1000)) {
            error = "connect failed: " + socket.errorString();
            return false;
        }

        static quint16 txIdSeed = 1;
        const quint16 txId = txIdSeed++;

        QByteArray frame;
        {
            QDataStream ds(&frame, QIODevice::WriteOnly);
            ds.setByteOrder(QDataStream::BigEndian);
            ds << txId << quint16(0) << quint16(pdu.size() + 2) << m_unitId;
        }
        frame.append(static_cast<char>(functionCode));
        frame.append(pdu);

        if (socket.write(frame) != frame.size()) {
            error = "write failed: " + socket.errorString();
            return false;
        }
        if (!socket.waitForBytesWritten(1000)) {
            error = "write timeout: " + socket.errorString();
            return false;
        }

        QByteArray mbap;
        if (!readExact(socket, 7, mbap, error)) {
            return false;
        }

        quint16 rxTxId = 0;
        quint16 protocolId = 0;
        quint16 length = 0;
        quint8 rxUnitId = 0;
        {
            QDataStream ds(mbap);
            ds.setByteOrder(QDataStream::BigEndian);
            ds >> rxTxId >> protocolId >> length >> rxUnitId;
        }
        if (rxTxId != txId || protocolId != 0 || rxUnitId != m_unitId || length < 2) {
            error = "invalid mbap response";
            return false;
        }

        QByteArray body;
        if (!readExact(socket, static_cast<int>(length) - 1, body, error)) {
            return false;
        }
        responseFunction = static_cast<quint8>(body.at(0));
        responseData = body.mid(1);
        return true;
    }

    std::vector<quint16> readHoldingRegisters(quint16 address, quint16 count, QString& error) const {
        QByteArray pdu;
        {
            QDataStream ds(&pdu, QIODevice::WriteOnly);
            ds.setByteOrder(QDataStream::BigEndian);
            ds << address << count;
        }

        quint8 fc = 0;
        QByteArray data;
        if (!modbusRequest(0x03, pdu, fc, data, error)) {
            return {};
        }
        if (fc != 0x03 || data.isEmpty()) {
            error = QString("unexpected function code: %1").arg(fc);
            return {};
        }
        const int expectedBytes = static_cast<int>(count) * 2;
        const int byteCount = static_cast<int>(static_cast<quint8>(data.at(0)));
        if (byteCount != expectedBytes || data.size() < 1 + expectedBytes) {
            error = QString("invalid byte count: %1").arg(byteCount);
            return {};
        }

        std::vector<quint16> regs;
        regs.reserve(count);
        for (int i = 0; i < count; ++i) {
            const int offset = 1 + i * 2;
            const quint8 hi = static_cast<quint8>(data.at(offset));
            const quint8 lo = static_cast<quint8>(data.at(offset + 1));
            regs.push_back(static_cast<quint16>((hi << 8) | lo));
        }
        return regs;
    }

    QString m_exePath;
    quint16 m_port = 0;
    quint8 m_unitId = 1;
    QProcessEnvironment m_env;
    QProcess m_proc;
    QString m_error;
};

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <vector>

#include "driver_plc_crane_sim/handler.h"
#include "driver_plc_crane_sim/sim_device.h"
#include "stdiolink/driver/mock_responder.h"

namespace {

const stdiolink::MockResponder::Response* findLastStatus(
        const std::vector<stdiolink::MockResponder::Response>& responses,
        const QString& status) {
    for (auto it = responses.rbegin(); it != responses.rend(); ++it) {
        if (it->status == status) {
            return &(*it);
        }
    }
    return nullptr;
}

int countStatus(const std::vector<stdiolink::MockResponder::Response>& responses,
                const QString& status) {
    int count = 0;
    for (const auto& item : responses) {
        if (item.status == status) {
            ++count;
        }
    }
    return count;
}

int countEvent(const std::vector<stdiolink::MockResponder::Response>& responses,
               const QString& eventName) {
    int count = 0;
    for (const auto& item : responses) {
        if (item.status == "event" && item.eventName == eventName) {
            ++count;
        }
    }
    return count;
}

QString exeSuffix() {
#ifdef Q_OS_WIN
    return ".exe";
#else
    return QString();
#endif
}

QString findPlcCraneSimExe() {
    const QString driverName = "stdio.drv.plc_crane_sim";
    const QString exeName = driverName + exeSuffix();
    const QString binDir = QCoreApplication::applicationDirPath();

    const QString runtimePath = QDir(binDir + "/..").absoluteFilePath(
        "data_root/drivers/" + driverName + "/" + exeName);
    if (QFile::exists(runtimePath)) {
        return runtimePath;
    }

    const QString rawPath = QDir(binDir).absoluteFilePath(exeName);
    if (QFile::exists(rawPath)) {
        return rawPath;
    }

    return {};
}

QProcessEnvironment childProcessEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString binDir = QCoreApplication::applicationDirPath();
    const QString oldPath = env.value("PATH");
    env.insert("PATH", oldPath.isEmpty() ? binDir : (binDir + QDir::listSeparator() + oldPath));
    return env;
}

bool readExact(QTcpSocket& sock, int size, QByteArray& out, QString& err, int timeoutMs = 2000) {
    out.clear();
    while (out.size() < size) {
        if (sock.bytesAvailable() <= 0 && !sock.waitForReadyRead(timeoutMs)) {
            err = QString("read timeout: need %1 bytes, got %2").arg(size).arg(out.size());
            return false;
        }
        out.append(sock.read(size - out.size()));
    }
    return true;
}

bool modbusRequest(const QString& host, quint16 port, quint8 unitId, quint8 functionCode,
                   const QByteArray& pdu, quint8& responseFc, QByteArray& responseData,
                   QString& err) {
    QTcpSocket sock;
    sock.connectToHost(host, port);
    if (!sock.waitForConnected(2000)) {
        err = QString("connect failed: %1").arg(sock.errorString());
        return false;
    }

    static quint16 txIdSeed = 1;
    const quint16 txId = txIdSeed++;

    QByteArray frame;
    {
        QDataStream ds(&frame, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << txId << quint16(0) << quint16(pdu.size() + 2) << unitId;
    }
    frame.append(static_cast<char>(functionCode));
    frame.append(pdu);

    if (sock.write(frame) != frame.size()) {
        err = QString("write failed: %1").arg(sock.errorString());
        return false;
    }
    if (!sock.waitForBytesWritten(2000)) {
        err = QString("write timeout: %1").arg(sock.errorString());
        return false;
    }

    QByteArray mbap;
    if (!readExact(sock, 7, mbap, err)) {
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

    if (rxTxId != txId || protocolId != 0 || rxUnitId != unitId || length < 2) {
        err = "invalid mbap response";
        return false;
    }

    QByteArray body;
    if (!readExact(sock, static_cast<int>(length) - 1, body, err)) {
        return false;
    }
    if (body.isEmpty()) {
        err = "empty modbus response body";
        return false;
    }

    responseFc = static_cast<quint8>(body.at(0));
    responseData = body.mid(1);
    return true;
}

bool writeSingleRegister(const QString& host, quint16 port, quint8 unitId,
                         quint16 address, quint16 value, QString& err) {
    QByteArray pdu;
    {
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << address << value;
    }

    quint8 fc = 0;
    QByteArray data;
    if (!modbusRequest(host, port, unitId, 0x06, pdu, fc, data, err)) {
        return false;
    }

    if (fc != 0x06 || data != pdu) {
        err = QString("unexpected fc/data in write single response, fc=%1").arg(fc);
        return false;
    }
    return true;
}

std::vector<quint16> readHoldingRegisters(const QString& host, quint16 port, quint8 unitId,
                                          quint16 address, quint16 count, QString& err) {
    QByteArray pdu;
    {
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << address << count;
    }

    quint8 fc = 0;
    QByteArray data;
    if (!modbusRequest(host, port, unitId, 0x03, pdu, fc, data, err)) {
        return {};
    }

    if (fc != 0x03 || data.isEmpty()) {
        err = QString("unexpected fc in read holding response: %1").arg(fc);
        return {};
    }

    const int expectedBytes = static_cast<int>(count) * 2;
    const int byteCount = static_cast<int>(static_cast<quint8>(data.at(0)));
    if (byteCount != expectedBytes || data.size() < 1 + expectedBytes) {
        err = QString("invalid read holding payload byte count: %1").arg(byteCount);
        return {};
    }

    std::vector<quint16> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int offset = 1 + i * 2;
        const quint8 hi = static_cast<quint8>(data.at(offset));
        const quint8 lo = static_cast<quint8>(data.at(offset + 1));
        out.push_back(static_cast<quint16>((hi << 8) | lo));
    }
    return out;
}

std::vector<bool> readDiscreteInputs(const QString& host, quint16 port, quint8 unitId,
                                     quint16 address, quint16 count, QString& err) {
    QByteArray pdu;
    {
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << address << count;
    }

    quint8 fc = 0;
    QByteArray data;
    if (!modbusRequest(host, port, unitId, 0x02, pdu, fc, data, err)) {
        return {};
    }

    if (fc != 0x02 || data.isEmpty()) {
        err = QString("unexpected fc in read discrete response: %1").arg(fc);
        return {};
    }

    const int byteCount = static_cast<int>(static_cast<quint8>(data.at(0)));
    if (data.size() < 1 + byteCount) {
        err = "invalid read discrete payload";
        return {};
    }

    std::vector<bool> bits;
    bits.reserve(count);
    const QByteArray payload = data.mid(1, byteCount);
    for (int i = 0; i < count; ++i) {
        const int byteIndex = i / 8;
        const int bitIndex = i % 8;
        if (byteIndex >= payload.size()) {
            bits.push_back(false);
            continue;
        }
        const quint8 byte = static_cast<quint8>(payload.at(byteIndex));
        bits.push_back(((byte >> bitIndex) & 0x01) == 0x01);
    }
    return bits;
}

SimDriverConfig makeFastConfig() {
    SimDriverConfig cfg;
    cfg.listenAddress = "127.0.0.1";
    cfg.listenPort = 0;
    cfg.unitId = 1;
    cfg.tickMs = 10;
    cfg.heartbeatMs = 100;
    cfg.cylinderUpDelayMs = 40;
    cfg.cylinderDownDelayMs = 40;
    cfg.valveOpenDelayMs = 40;
    cfg.valveCloseDelayMs = 40;
    return cfg;
}

quint16 allocateLocalPort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

bool startHandler(SimPlcCraneHandler& handler, stdiolink::MockResponder& resp) {
    resp.clear();
    handler.handle("run", QJsonObject{}, resp);
    return handler.isRunning();
}

} // namespace

class PlcCraneSimHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
    }
};

// T01 - run 首次启动成功
TEST_F(PlcCraneSimHandlerTest, T01_RunStartsAndEmitsStartedEvent) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;

    ASSERT_TRUE(startHandler(handler, resp));
    EXPECT_GT(static_cast<int>(handler.serverPort()), 0);
    EXPECT_EQ(countStatus(resp.responses, "done"), 0);
    EXPECT_EQ(countStatus(resp.responses, "error"), 0);
    EXPECT_EQ(countEvent(resp.responses, "started"), 1);
}

// T02 - run 重复调用失败
TEST_F(PlcCraneSimHandlerTest, T02_RunDuplicateReturnsError3) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, resp));

    resp.clear();
    handler.handle("run", QJsonObject{}, resp);
    const auto* lastError = findLastStatus(resp.responses, "error");
    ASSERT_NE(lastError, nullptr);
    EXPECT_EQ(lastError->code, 3);
}

// T03 - HR[0] 触发气缸上升/下降/中途 stop
TEST_F(PlcCraneSimHandlerTest, T03_HoldingRegister0DrivesCylinderState) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    QTest::qWait(120);
    bool diUp = false;
    bool diDown = false;
    ASSERT_TRUE(handler.readDiscreteInputForTest(9, diUp));
    ASSERT_TRUE(handler.readDiscreteInputForTest(10, diDown));
    EXPECT_TRUE(diUp);
    EXPECT_FALSE(diDown);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 2, err)) << err.toStdString();
    QTest::qWait(120);
    ASSERT_TRUE(handler.readDiscreteInputForTest(9, diUp));
    ASSERT_TRUE(handler.readDiscreteInputForTest(10, diDown));
    EXPECT_FALSE(diUp);
    EXPECT_TRUE(diDown);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    QTest::qWait(10);
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 0, err)) << err.toStdString();
    QTest::qWait(30);
    ASSERT_TRUE(handler.readDiscreteInputForTest(9, diUp));
    ASSERT_TRUE(handler.readDiscreteInputForTest(10, diDown));
    EXPECT_FALSE(diUp);
    EXPECT_FALSE(diDown);
}

// T04 - HR[1] 触发阀门打开/关闭
TEST_F(PlcCraneSimHandlerTest, T04_HoldingRegister1DrivesValveState) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 1, err)) << err.toStdString();
    QTest::qWait(120);
    bool diOpen = false;
    bool diClosed = false;
    ASSERT_TRUE(handler.readDiscreteInputForTest(13, diOpen));
    ASSERT_TRUE(handler.readDiscreteInputForTest(14, diClosed));
    EXPECT_TRUE(diOpen);
    EXPECT_FALSE(diClosed);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 2, err)) << err.toStdString();
    QTest::qWait(120);
    ASSERT_TRUE(handler.readDiscreteInputForTest(13, diOpen));
    ASSERT_TRUE(handler.readDiscreteInputForTest(14, diClosed));
    EXPECT_FALSE(diOpen);
    EXPECT_TRUE(diClosed);
}

// T05 - HR[2]/HR[3] 合法写入生效
TEST_F(PlcCraneSimHandlerTest, T05_HoldingRegister2And3AcceptValidValues) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(2, 1, err)) << err.toStdString();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(3, 1, err)) << err.toStdString();
    quint16 runValue = 0;
    quint16 modeValue = 0;
    ASSERT_TRUE(handler.readHoldingRegisterForTest(2, runValue));
    ASSERT_TRUE(handler.readHoldingRegisterForTest(3, modeValue));
    EXPECT_EQ(runValue, 1);
    EXPECT_EQ(modeValue, 1);
}

// T06 - 非法地址被拒绝
TEST_F(PlcCraneSimHandlerTest, T06_DeviceRejectsInvalidAddress) {
    SimPlcCraneDevice::Config cfg;
    SimPlcCraneDevice device(cfg);
    QString err;

    EXPECT_FALSE(device.writeHoldingRegister(99, 1, err));
    EXPECT_FALSE(err.isEmpty());
}

// T07 - 非法气缸动作值被拒绝且状态不变
TEST_F(PlcCraneSimHandlerTest, T07_DeviceRejectsInvalidCylinderValue) {
    SimPlcCraneDevice::Config cfg;
    SimPlcCraneDevice device(cfg);
    const auto before = device.cylinderState();

    QString err;
    EXPECT_FALSE(device.writeHoldingRegister(0, 7, err));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(device.cylinderState(), before);
}

// T08 - 子进程 run 持续输出 sim_heartbeat 事件
TEST_F(PlcCraneSimHandlerTest, T08_RunEmitsHeartbeatEventStream) {
    const QString exe = findPlcCraneSimExe();
    ASSERT_FALSE(exe.isEmpty()) << "stdio.drv.plc_crane_sim executable not found";

    const quint16 port = allocateLocalPort();
    ASSERT_GT(static_cast<int>(port), 0);
    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments({
        "--profile=oneshot",
        "--mode=console",
        "--cmd=run",
        "--listen-address=127.0.0.1",
        QString("--listen-port=%1").arg(port),
        "--heartbeat-ms=100"
    });
    proc.setProcessEnvironment(childProcessEnv());
    proc.start();
    ASSERT_TRUE(proc.waitForStarted(5000));

    int heartbeatCount = 0;
    bool startedSeen = false;
    bool doneSeen = false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && heartbeatCount < 2) {
        if (!proc.canReadLine()) {
            (void)proc.waitForReadyRead(200);
            continue;
        }
        while (proc.canReadLine()) {
            const QByteArray line = proc.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }

            QJsonParseError parseErr;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }

            const QJsonObject msg = doc.object();
            const QString status = msg.value("status").toString();
            if (status == "done") {
                doneSeen = true;
            }
            if (status != "event") {
                continue;
            }

            const QJsonObject evt = msg.value("data").toObject();
            const QString eventName = evt.value("event").toString();
            if (eventName == "started") {
                startedSeen = true;
            }
            if (eventName == "sim_heartbeat") {
                const QJsonObject payload = evt.value("data").toObject();
                EXPECT_TRUE(payload.contains("uptime_s"));
                ++heartbeatCount;
            }
        }
    }

    proc.kill();
    (void)proc.waitForFinished(3000);

    EXPECT_TRUE(startedSeen);
    EXPECT_GE(heartbeatCount, 2);
    EXPECT_FALSE(doneSeen);
}

// T09 - 未知命令返回 404
TEST_F(PlcCraneSimHandlerTest, T09_UnknownCommandReturns404) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;

    handler.handle("status", QJsonObject{}, resp);
    const auto* lastError = findLastStatus(resp.responses, "error");
    ASSERT_NE(lastError, nullptr);
    EXPECT_EQ(lastError->code, 404);
}

// T10 - 端口占用时 run 启动失败
TEST_F(PlcCraneSimHandlerTest, T10_RunFailsWhenPortOccupied) {
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));

    SimDriverConfig cfg = makeFastConfig();
    cfg.listenPort = static_cast<int>(blocker.serverPort());
    cfg.listenAddress = "127.0.0.1";
    SimPlcCraneHandler handler(cfg);
    stdiolink::MockResponder resp;

    handler.handle("run", QJsonObject{}, resp);
    const auto* lastError = findLastStatus(resp.responses, "error");
    ASSERT_NE(lastError, nullptr);
    EXPECT_EQ(lastError->code, 1);
    EXPECT_FALSE(handler.isRunning());
}

// T11 - 启动参数越界导致进程级退出码 3
TEST_F(PlcCraneSimHandlerTest, T11_InvalidArgReturnsProcessExitCode3) {
    const QString exe = findPlcCraneSimExe();
    ASSERT_FALSE(exe.isEmpty()) << "stdio.drv.plc_crane_sim executable not found";

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments({"--listen-port=70000"});
    proc.setProcessEnvironment(childProcessEnv());
    proc.start();

    ASSERT_TRUE(proc.waitForFinished(5000));
    EXPECT_EQ(proc.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(proc.exitCode(), 3);
}

// R01 - 仅 run 后通过 Modbus 完成控制链路
TEST_F(PlcCraneSimHandlerTest, R01_RunOnlyThenModbusControlPathWorks) {
    const QString exe = findPlcCraneSimExe();
    ASSERT_FALSE(exe.isEmpty()) << "stdio.drv.plc_crane_sim executable not found";
    const quint16 port = allocateLocalPort();
    ASSERT_GT(static_cast<int>(port), 0);

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments({
        "--profile=oneshot",
        "--mode=console",
        "--cmd=run",
        "--listen-address=127.0.0.1",
        QString("--listen-port=%1").arg(port),
        "--unit-id=1",
        "--tick-ms=10",
        "--cylinder-up-delay=40"
    });
    proc.setProcessEnvironment(childProcessEnv());
    proc.start();
    ASSERT_TRUE(proc.waitForStarted(5000));

    QElapsedTimer readyTimer;
    readyTimer.start();
    bool startedSeen = false;
    while (readyTimer.elapsed() < 3000 && !startedSeen) {
        if (!proc.canReadLine()) {
            (void)proc.waitForReadyRead(100);
            continue;
        }
        while (proc.canReadLine()) {
            const QByteArray line = proc.readLine().trimmed();
            QJsonParseError parseErr;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }
            const QJsonObject msg = doc.object();
            if (msg.value("status").toString() != "event") {
                continue;
            }
            const QJsonObject evt = msg.value("data").toObject();
            if (evt.value("event").toString() == "started") {
                startedSeen = true;
                break;
            }
        }
    }
    ASSERT_TRUE(startedSeen) << "missing started event";

    QString err;
    ASSERT_TRUE(writeSingleRegister("127.0.0.1", port, 1, 0, 1, err)) << err.toStdString();
    QTest::qWait(120);

    const auto bits = readDiscreteInputs("127.0.0.1", port, 1, 9, 2, err);
    ASSERT_EQ(bits.size(), 2U) << err.toStdString();
    EXPECT_TRUE(bits[0]);
    EXPECT_FALSE(bits[1]);

    proc.kill();
    (void)proc.waitForFinished(3000);
}

// T12 - --arg- 前缀参数应被正确解析
TEST_F(PlcCraneSimHandlerTest, T12_ParseConfigSupportsArgPrefixedOptions) {
    SimDriverConfig cfg;
    QString err;

    std::vector<QByteArray> argsBytes{
        QByteArray("stdio.drv.plc_crane_sim"),
        QByteArray("--arg-listen-port=1602"),
        QByteArray("--arg-unit-id=2"),
        QByteArray("--arg-event-mode=all")
    };
    std::vector<char*> argv;
    argv.reserve(argsBytes.size());
    for (QByteArray& item : argsBytes) {
        argv.push_back(item.data());
    }

    EXPECT_TRUE(parseSimDriverConfigArgs(static_cast<int>(argv.size()), argv.data(), cfg, &err))
        << err.toStdString();
    EXPECT_EQ(cfg.listenPort, 1602);
    EXPECT_EQ(static_cast<int>(cfg.unitId), 2);
    EXPECT_EQ(cfg.eventMode, "all");
}

// T14 - 未识别 --arg- 参数应原样透传给 DriverCore
TEST_F(PlcCraneSimHandlerTest, T14_UnknownArgPrefixedOptionKeepsOriginalToken) {
    SimDriverConfig cfg;
    QString err;
    QStringList passthroughArgs;

    std::vector<QByteArray> argsBytes{
        QByteArray("stdio.drv.plc_crane_sim"),
        QByteArray("--arg-mode=console"),
        QByteArray("--arg-custom"),
        QByteArray("custom_value"),
        QByteArray("--foo=bar")
    };
    std::vector<char*> argv;
    argv.reserve(argsBytes.size());
    for (QByteArray& item : argsBytes) {
        argv.push_back(item.data());
    }

    EXPECT_TRUE(parseSimDriverConfigArgs(
        static_cast<int>(argv.size()), argv.data(), cfg, &err, &passthroughArgs))
        << err.toStdString();

    ASSERT_GE(passthroughArgs.size(), 5);
    EXPECT_EQ(passthroughArgs[1], "--arg-mode=console");
    EXPECT_EQ(passthroughArgs[2], "--arg-custom");
    EXPECT_EQ(passthroughArgs[3], "custom_value");
    EXPECT_EQ(passthroughArgs[4], "--foo=bar");
}

// T13 - 气缸和阀门并发动作路径
TEST_F(PlcCraneSimHandlerTest, T13_CylinderAndValveCanMoveConcurrently) {
    SimPlcCraneHandler handler(makeFastConfig());
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 1, err)) << err.toStdString();

    QTest::qWait(140);
    bool diUp = false;
    bool diDown = false;
    bool diOpen = false;
    bool diClosed = false;
    ASSERT_TRUE(handler.readDiscreteInputForTest(9, diUp));
    ASSERT_TRUE(handler.readDiscreteInputForTest(10, diDown));
    ASSERT_TRUE(handler.readDiscreteInputForTest(13, diOpen));
    ASSERT_TRUE(handler.readDiscreteInputForTest(14, diClosed));

    EXPECT_TRUE(diUp);
    EXPECT_FALSE(diDown);
    EXPECT_TRUE(diOpen);
    EXPECT_FALSE(diClosed);
}

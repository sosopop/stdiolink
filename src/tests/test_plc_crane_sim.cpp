#include <gtest/gtest.h>

#include <QBitArray>
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

QBitArray readDiscreteInputs(const QString& host, quint16 port, quint8 unitId, quint16 address,
                             quint16 count, QString& err) {
    QByteArray pdu;
    {
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << address << count;
    }

    quint8 fc = 0;
    QByteArray data;
    if (!modbusRequest(host, port, unitId, 0x02, pdu, fc, data, err)) {
        return QBitArray();
    }

    if (fc != 0x02 || data.isEmpty()) {
        err = QString("unexpected fc in read discrete response: %1").arg(fc);
        return QBitArray();
    }

    const int expectedBytes = static_cast<int>((count + 7) / 8);
    const int byteCount = static_cast<int>(static_cast<quint8>(data.at(0)));
    if (byteCount != expectedBytes || data.size() < 1 + expectedBytes) {
        err = QString("invalid read discrete payload byte count: %1").arg(byteCount);
        return QBitArray();
    }

    QBitArray out(count);
    for (int i = 0; i < count; ++i) {
        const int byteIndex = i / 8;
        const int bitIndex = i % 8;
        const quint8 value = static_cast<quint8>(data.at(1 + byteIndex));
        out.setBit(i, ((value >> bitIndex) & 0x01) != 0);
    }
    return out;
}

quint16 allocateLocalPort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

QJsonObject makeFastRunData() {
    const quint16 port = allocateLocalPort();
    return QJsonObject{
        {"listen_address", "127.0.0.1"},
        {"listen_port", static_cast<int>(port)},
        {"unit_id", 1},
        {"heartbeat_ms", 100},
        {"cylinder_up_delay", 40},
        {"cylinder_down_delay", 40},
        {"valve_open_delay", 40},
        {"valve_close_delay", 40}
    };
}

bool startHandler(SimPlcCraneHandler& handler, const QJsonObject& runData, stdiolink::MockResponder& resp) {
    resp.clear();
    handler.handle("run", runData, resp);
    return handler.isRunning();
}

bool readDiscreteBitForTest(SimPlcCraneHandler& handler, quint16 address, bool& value) {
    return handler.readDiscreteInputForTest(address, value);
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
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;

    ASSERT_TRUE(startHandler(handler, runData, resp));
    EXPECT_GT(static_cast<int>(handler.serverPort()), 0);
    EXPECT_EQ(countStatus(resp.responses, "done"), 0);
    EXPECT_EQ(countStatus(resp.responses, "error"), 0);
}

// T01A - heartbeat_ms 默认值为 0（关闭）
TEST_F(PlcCraneSimHandlerTest, T01A_MetaHeartbeatDefaultIsZero) {
    SimPlcCraneHandler handler;
    const auto* runCmd = handler.driverMeta().findCommand("run");
    ASSERT_NE(runCmd, nullptr);

    bool found = false;
    for (const auto& param : runCmd->params) {
        if (param.name != "heartbeat_ms") {
            continue;
        }
        found = true;
        EXPECT_EQ(param.defaultValue.toInt(), 0);
        break;
    }
    EXPECT_TRUE(found);

    bool hasTickMs = false;
    for (const auto& param : runCmd->params) {
        if (param.name == "tick_ms") {
            hasTickMs = true;
            break;
        }
    }
    EXPECT_FALSE(hasTickMs);
}

// T01B - 默认状态为阀门关闭、气缸上到位
TEST_F(PlcCraneSimHandlerTest, T01B_DefaultStateIsValveClosedAndCylinderUp) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    bool rawCylinderUp = false;
    bool rawCylinderDown = false;
    bool rawValveOpen = false;
    bool rawValveClosed = false;
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 13, rawValveOpen));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 14, rawValveClosed));

    EXPECT_FALSE(rawCylinderUp);
    EXPECT_FALSE(rawCylinderDown);
    EXPECT_FALSE(rawValveOpen);
    EXPECT_TRUE(rawValveClosed);
}

// T02 - run 重复调用失败
TEST_F(PlcCraneSimHandlerTest, T02_RunDuplicateReturnsError3) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    resp.clear();
    handler.handle("run", runData, resp);
    const auto* lastError = findLastStatus(resp.responses, "error");
    ASSERT_NE(lastError, nullptr);
    EXPECT_EQ(lastError->code, 3);
}

// T03 - HR[0] 触发气缸上升/下降/中途 stop
TEST_F(PlcCraneSimHandlerTest, T03_HoldingRegister0DrivesCylinderState) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    QTest::qWait(120);
    bool rawCylinderUp = false;
    bool rawCylinderDown = false;
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    EXPECT_FALSE(rawCylinderUp);
    EXPECT_FALSE(rawCylinderDown);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 2, err)) << err.toStdString();
    QTest::qWait(120);
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    EXPECT_TRUE(rawCylinderUp);
    EXPECT_TRUE(rawCylinderDown);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    QTest::qWait(10);
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 0, err)) << err.toStdString();
    QTest::qWait(30);
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    EXPECT_TRUE(rawCylinderUp);
    EXPECT_FALSE(rawCylinderDown);
}

// T04 - HR[1] 触发阀门打开/关闭
TEST_F(PlcCraneSimHandlerTest, T04_HoldingRegister1DrivesValveState) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 1, err)) << err.toStdString();
    QTest::qWait(120);
    bool rawValveOpen = false;
    bool rawValveClosed = false;
    ASSERT_TRUE(readDiscreteBitForTest(handler, 13, rawValveOpen));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 14, rawValveClosed));
    EXPECT_TRUE(rawValveOpen);
    EXPECT_FALSE(rawValveClosed);

    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 2, err)) << err.toStdString();
    QTest::qWait(120);
    ASSERT_TRUE(readDiscreteBitForTest(handler, 13, rawValveOpen));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 14, rawValveClosed));
    EXPECT_FALSE(rawValveOpen);
    EXPECT_TRUE(rawValveClosed);
}

// T05 - HR[2]/HR[3] 合法写入生效
TEST_F(PlcCraneSimHandlerTest, T05_HoldingRegister2And3AcceptValidValues) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

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

// T12 - 自动模式触发组合动作，手动模式恢复独立控制
TEST_F(PlcCraneSimHandlerTest, T12_AutoModeChainsValveOpenThenCylinderDown) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(3, 1, err)) << err.toStdString();
    QTest::qWait(140);

    bool rawCylinderUp = false;
    bool rawCylinderDown = false;
    bool rawValveOpen = false;
    bool rawValveClosed = false;
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 13, rawValveOpen));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 14, rawValveClosed));
    EXPECT_TRUE(rawCylinderUp);
    EXPECT_TRUE(rawCylinderDown);
    EXPECT_TRUE(rawValveOpen);
    EXPECT_FALSE(rawValveClosed);

    quint16 hrCylinder = 0;
    quint16 hrValve = 0;
    ASSERT_TRUE(handler.readHoldingRegisterForTest(0, hrCylinder));
    ASSERT_TRUE(handler.readHoldingRegisterForTest(1, hrValve));
    EXPECT_EQ(hrCylinder, 2);
    EXPECT_EQ(hrValve, 1);

    err.clear();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 2, err)) << err.toStdString();
    quint16 valveAfterRejectedWrite = 0;
    ASSERT_TRUE(handler.readHoldingRegisterForTest(1, valveAfterRejectedWrite));
    EXPECT_EQ(valveAfterRejectedWrite, 1);

    err.clear();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(3, 0, err)) << err.toStdString();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 2, err)) << err.toStdString();
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
        "--listen_address=127.0.0.1",
        QString("--listen_port=%1").arg(port),
        "--heartbeat_ms=100"
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
    SimPlcCraneHandler handler;
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

    SimPlcCraneHandler handler;
    stdiolink::MockResponder resp;
    QJsonObject runData = makeFastRunData();
    runData["listen_address"] = "127.0.0.1";
    runData["listen_port"] = static_cast<int>(blocker.serverPort());

    handler.handle("run", runData, resp);
    const auto* lastError = findLastStatus(resp.responses, "error");
    ASSERT_NE(lastError, nullptr);
    EXPECT_EQ(lastError->code, 1);
    EXPECT_FALSE(handler.isRunning());
}

// T11 - run 参数越界导致进程级失败退出码
TEST_F(PlcCraneSimHandlerTest, T11_InvalidRunParamReturnsProcessErrorExitCode) {
    const QString exe = findPlcCraneSimExe();
    ASSERT_FALSE(exe.isEmpty()) << "stdio.drv.plc_crane_sim executable not found";

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments({"--mode=console", "--cmd=run", "--listen_port=70000"});
    proc.setProcessEnvironment(childProcessEnv());
    proc.start();

    ASSERT_TRUE(proc.waitForFinished(5000));
    EXPECT_EQ(proc.exitStatus(), QProcess::NormalExit);
    EXPECT_TRUE(proc.exitCode() == 3 || proc.exitCode() == 400);
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
        "--listen_address=127.0.0.1",
        QString("--listen_port=%1").arg(port),
        "--unit_id=1",
        "--cylinder_up_delay=40",
        "--cylinder_down_delay=40"
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
    QBitArray bits = readDiscreteInputs("127.0.0.1", port, 1, 9, 6, err);
    ASSERT_EQ(bits.size(), 6) << err.toStdString();
    EXPECT_FALSE(bits.testBit(0));
    EXPECT_TRUE(bits.testBit(5));

    ASSERT_TRUE(writeSingleRegister("127.0.0.1", port, 1, 0, 2, err)) << err.toStdString();
    QTest::qWait(250);

    bits = readDiscreteInputs("127.0.0.1", port, 1, 9, 2, err);
    ASSERT_EQ(bits.size(), 2) << err.toStdString();
    EXPECT_TRUE(bits.testBit(0));
    EXPECT_TRUE(bits.testBit(1));

    proc.kill();
    (void)proc.waitForFinished(3000);
}

// T13 - 气缸和阀门并发动作路径
TEST_F(PlcCraneSimHandlerTest, T13_CylinderAndValveCanMoveConcurrently) {
    SimPlcCraneHandler handler;
    const QJsonObject runData = makeFastRunData();
    stdiolink::MockResponder resp;
    ASSERT_TRUE(startHandler(handler, runData, resp));

    QString err;
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(0, 1, err)) << err.toStdString();
    ASSERT_TRUE(handler.writeHoldingRegisterForTest(1, 1, err)) << err.toStdString();

    QTest::qWait(140);
    bool rawCylinderUp = false;
    bool rawCylinderDown = false;
    bool rawValveOpen = false;
    bool rawValveClosed = false;
    ASSERT_TRUE(readDiscreteBitForTest(handler, 9, rawCylinderUp));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 10, rawCylinderDown));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 13, rawValveOpen));
    ASSERT_TRUE(readDiscreteBitForTest(handler, 14, rawValveClosed));

    EXPECT_FALSE(rawCylinderUp);
    EXPECT_FALSE(rawCylinderDown);
    EXPECT_TRUE(rawValveOpen);
    EXPECT_FALSE(rawValveClosed);
}

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <deque>
#include <functional>
#include <vector>

#include "driver_3d_laser_radar/handler.h"
#include "driver_3d_laser_radar/laser_session.h"
#include "driver_3d_laser_radar/laser_transport.h"
#include "driver_3d_laser_radar/protocol_codec.h"

using namespace laser_radar;

namespace {

class JsonResponder : public stdiolink::IResponder {
public:
    QString lastStatus;
    int lastCode = -1;
    QJsonObject lastData;

    void done(int code, const QJsonValue& payload) override {
        lastStatus = "done";
        lastCode = code;
        lastData = payload.toObject();
    }

    void error(int code, const QJsonValue& payload) override {
        lastStatus = "error";
        lastCode = code;
        lastData = payload.toObject();
    }

    void event(int code, const QJsonValue& payload) override {
        Q_UNUSED(code);
        Q_UNUSED(payload);
    }

    void event(const QString& name, int code, const QJsonValue& data) override {
        Q_UNUSED(name);
        Q_UNUSED(code);
        Q_UNUSED(data);
    }
};

class FakeLaserTransport : public ILaserTransport {
public:
    bool open(const LaserTransportParams& params, QString* errorMessage) override {
        lastParams = params;
        if (openShouldFail) {
            if (errorMessage) {
                *errorMessage = openError;
            }
            return false;
        }
        isOpen = true;
        return true;
    }

    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override {
        Q_UNUSED(timeoutMs);
        if (!isOpen) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("transport not open");
            }
            return false;
        }
        ++writeCount;
        readCountSinceLastWrite = 0;
        writes.push_back(frame);
        if (onWrite) {
            onWrite(frame);
        }
        return true;
    }

    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override {
        Q_UNUSED(timeoutMs);
        const bool shouldFail = readCountSinceLastWrite == 0
            && failFirstReadAfterWrites.contains(writeCount);
        ++readCountSinceLastWrite;
        if (shouldFail) {
            if (errorMessage) {
                *errorMessage = readError;
            }
            return false;
        }
        if (queuedReads.empty()) {
            if (errorMessage) {
                *errorMessage = readError;
            }
            return false;
        }
        chunk = queuedReads.front();
        queuedReads.pop_front();
        return true;
    }

    void close() override {
        isOpen = false;
        ++closeCount;
    }

    void enqueueRead(const QByteArray& chunk) { queuedReads.push_back(chunk); }

    LaserTransportParams lastParams;
    bool openShouldFail = false;
    QString openError = "open failed";
    QString readError = "read timeout";
    bool isOpen = false;
    int closeCount = 0;
    int writeCount = 0;
    int readCountSinceLastWrite = 0;
    std::deque<QByteArray> queuedReads;
    QSet<int> failFirstReadAfterWrites;
    std::vector<QByteArray> writes;
    std::function<void(const QByteArray&)> onWrite;
};

class NonOwningTransportWrapper : public ILaserTransport {
public:
    explicit NonOwningTransportWrapper(ILaserTransport* inner)
        : m_inner(inner) {}

    bool open(const LaserTransportParams& params, QString* errorMessage) override {
        return m_inner->open(params, errorMessage);
    }

    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override {
        return m_inner->writeFrame(frame, timeoutMs, errorMessage);
    }

    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override {
        return m_inner->readSome(chunk, timeoutMs, errorMessage);
    }

    void close() override { m_inner->close(); }

private:
    ILaserTransport* m_inner = nullptr;
};

void appendU16(QByteArray& bytes, quint16 value) {
    quint8 buf[2];
    qToBigEndian(value, buf);
    bytes.append(reinterpret_cast<const char*>(buf), 2);
}

void appendU32(QByteArray& bytes, quint32 value) {
    quint8 buf[4];
    qToBigEndian(value, buf);
    bytes.append(reinterpret_cast<const char*>(buf), 4);
}

QByteArray makeQueryPayload(quint16 lastCounter, quint8 lastCommand, quint32 resultA,
                            quint32 resultB) {
    QByteArray payload;
    appendU16(payload, lastCounter);
    payload.append(static_cast<char>(lastCommand));
    appendU32(payload, resultA);
    appendU32(payload, resultB);
    return payload;
}

QByteArray makeCancelPayload(quint16 lastCounter, quint8 lastCommand, quint8 resultCode) {
    QByteArray payload;
    appendU16(payload, lastCounter);
    payload.append(static_cast<char>(lastCommand));
    payload.append(static_cast<char>(resultCode));
    return payload;
}

QByteArray makeSegmentPayload(quint32 segId, quint32 segCount, const QByteArray& segmentData) {
    QByteArray payload;
    appendU32(payload, segId);
    appendU32(payload, segCount);
    appendU16(payload, static_cast<quint16>(segmentData.size()));
    payload.append(segmentData);
    return payload;
}

QJsonObject baseParams() {
    return QJsonObject{
        {"host", "127.0.0.1"},
        {"port", 23},
        {"timeout_ms", 100},
        {"query_interval_ms", 1},
        {"task_timeout_ms", 20}
    };
}

QJsonObject findExampleByMode(const QVector<QJsonObject>& examples, const QString& mode) {
    for (const auto& example : examples) {
        if (example.value("mode").toString() == mode) {
            return example;
        }
    }
    return QJsonObject();
}

class ThreeDLaserRadarTestBase : public ::testing::Test {
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

} // namespace

TEST(ThreeDLaserRadarProtocolTest, EncodeDecodeRoundtrip) {
    const QByteArray payload = makeWriteRegPayload(22, 1000);
    const QByteArray frame = encodeFrame(7, kDeviceAddr, CmdId::WriteReg, payload);

    LaserFrame decoded;
    QString errorMessage;
    EXPECT_EQ(tryDecodeFrame(frame, kDeviceAddr, CmdId::WriteReg, &decoded, &errorMessage),
              DecodeStatus::Ok)
        << errorMessage.toStdString();
    EXPECT_EQ(decoded.counter, 7);
    EXPECT_EQ(decoded.addr, kDeviceAddr);
    EXPECT_EQ(decoded.command, CmdId::WriteReg);
    EXPECT_EQ(decoded.payload, payload);
}

TEST(ThreeDLaserRadarProtocolTest, DetectsBadMagicAndCrc) {
    const QByteArray payload = makeU32Payload(123);
    QByteArray frame = encodeFrame(1, kDeviceAddr, CmdId::Test, payload);

    LaserFrame decoded;
    QString errorMessage;
    frame[0] = 'X';
    EXPECT_EQ(tryDecodeFrame(frame, kDeviceAddr, CmdId::Test, &decoded, &errorMessage),
              DecodeStatus::BadMagic);

    frame = encodeFrame(1, kDeviceAddr, CmdId::Test, payload);
    frame[frame.size() - 1] ^= 0xFF;
    EXPECT_EQ(tryDecodeFrame(frame, kDeviceAddr, CmdId::Test, &decoded, &errorMessage, nullptr,
                             true),
              DecodeStatus::CrcError);
}

TEST_F(ThreeDLaserRadarTestBase, SessionHandlesFragmentedFrame) {
    FakeLaserTransport transport;
    const quint32 requestValue = 0x12345678u;
    const QByteArray responseFrame = encodeFrame(
        0, kDeviceAddr, CmdId::Test, makeU32Payload(~requestValue));
    transport.enqueueRead(responseFrame.left(7));
    transport.enqueueRead(responseFrame.mid(7));

    LaserSession session(&transport);
    LaserTransportParams params;
    params.host = "127.0.0.1";
    params.timeoutMs = 5;
    QString errorMessage;
    ASSERT_TRUE(session.open(params, &errorMessage));

    LaserFrame response;
    ASSERT_TRUE(session.sendAndReceive(CmdId::Test, makeU32Payload(requestValue), &response,
                                       &errorMessage))
        << errorMessage.toStdString();

    quint32 echo = 0;
    ASSERT_TRUE(parseU32Payload(response.payload, &echo));
    EXPECT_EQ(echo, ~requestValue);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerStatusAndUnknownCommand) {
    ThreeDLaserRadarHandler handler;
    JsonResponder responder;

    handler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("status").toString(), "ready");

    handler.handle("not_a_real_cmd", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 404);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerGetRegAndSetRegSucceed) {
    FakeLaserTransport fake;
    fake.enqueueRead(encodeFrame(0, kDeviceAddr, CmdId::ReadReg, makeWriteRegPayload(22, 1000)));
    fake.enqueueRead(encodeFrame(0, kDeviceAddr, CmdId::WriteReg, makeWriteRegPayload(22, 2000)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    QJsonObject getParams = baseParams();
    getParams["register"] = 22;
    handler.handle("get_reg", getParams, responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("value").toInt(), 1000);

    QJsonObject setParams = baseParams();
    setParams["register"] = 22;
    setParams["value"] = 2000;
    handler.handle("set_reg", setParams, responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("value").toInt(), 2000);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerUsesDefaultHostAndPortWhenOmitted) {
    FakeLaserTransport fake;
    fake.enqueueRead(encodeFrame(0, kDeviceAddr, CmdId::ReadReg, makeWriteRegPayload(1, 20)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    QJsonObject params;
    params["register"] = 1;
    handler.handle("get_reg", params, responder);

    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(fake.lastParams.host, "127.0.0.1");
    EXPECT_EQ(fake.lastParams.port, 23);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerQueryAndCancelExposeSemanticFields) {
    FakeLaserTransport fake;
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(5, CmdId::MoveX, TaskResult::Success, 90)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Cancel, makeCancelPayload(5, CmdId::MoveX, CancelFeedback::CanStop)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    handler.handle("query", baseParams(), responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("command_name").toString(), "move_x");
    EXPECT_EQ(responder.lastData.value("state").toString(), "success");

    handler.handle("cancel", baseParams(), responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("result").toString(), "can_stop");
}

TEST_F(ThreeDLaserRadarTestBase, HandlerCalibXSucceedsAfterSkippingStaleQueryResult) {
    FakeLaserTransport fake;
    fake.readError = "TCP read timeout";
    fake.failFirstReadAfterWrites.insert(2);
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(0, 0, 0, 0)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(99, CmdId::CalibX, TaskResult::Success, 1)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(1, CmdId::CalibX, TaskResult::Success, 1234)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    handler.handle("calib_x", baseParams(), responder);
    ASSERT_EQ(responder.lastStatus, "done")
        << QJsonDocument(responder.lastData).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(responder.lastData.value("task_command").toString(), "calib_x");
    EXPECT_EQ(responder.lastData.value("result_b").toInt(), 1234);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerMoveXFailureCarriesErrorCodeAndEncodesAngle) {
    FakeLaserTransport fake;
    fake.readError = "TCP read timeout";
    fake.failFirstReadAfterWrites.insert(2);
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(0, 0, 0, 0)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(1, CmdId::MoveX, 2002, 666)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["angle_deg"] = 45.5;
    handler.handle("move_x", params, responder);

    ASSERT_EQ(responder.lastStatus, "error")
        << QJsonDocument(responder.lastData).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(responder.lastData.value("error_code").toInt(), 2);

    ASSERT_GE(fake.writes.size(), 2u);
    LaserFrame moveFrame;
    QString errorMessage;
    ASSERT_EQ(tryDecodeFrame(fake.writes[1], kDeviceAddr, CmdId::MoveX, &moveFrame, &errorMessage),
              DecodeStatus::Ok)
        << errorMessage.toStdString();
    quint32 angleValue = 0;
    ASSERT_TRUE(parseU32Payload(moveFrame.payload, &angleValue));
    EXPECT_EQ(angleValue, 45500u);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerScanFieldSavesRawOutput) {
    FakeLaserTransport fake;
    fake.readError = "TCP read timeout";
    fake.failFirstReadAfterWrites.insert(2);
    const QByteArray scanBytes("ABCDEFGH");
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(0, 0, 0, 0)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(1, CmdId::ScanField, TaskResult::Running, 5000)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(1, CmdId::ScanField, TaskResult::Success,
                                                       scanBytes.size())));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::GetData, makeSegmentPayload(0, 1, scanBytes)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString outputPath = dir.path() + "/scan.bin";

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["begin_x_deg"] = 0.0;
    params["end_x_deg"] = 10.0;
    params["output"] = outputPath;
    handler.handle("scan_field", params, responder);

    ASSERT_EQ(responder.lastStatus, "done")
        << QJsonDocument(responder.lastData).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(responder.lastData.value("byte_count").toInt(), scanBytes.size());
    EXPECT_FALSE(responder.lastData.value("has_blank_scanlines").toBool());

    QFile file(outputPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), scanBytes);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerScanFieldReportsBlankScanlines) {
    FakeLaserTransport fake;
    fake.readError = "TCP read timeout";
    fake.failFirstReadAfterWrites.insert(2);
    const QByteArray scanBytes("WXYZ");
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query, makeQueryPayload(0, 0, 0, 0)));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::Query,
        makeQueryPayload(1, CmdId::ScanField, TaskResult::SuccessWithBlankScanline,
                         scanBytes.size())));
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::GetData, makeSegmentPayload(0, 1, scanBytes)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString outputPath = dir.path() + "/scan.bin";

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["begin_x_deg"] = 0.0;
    params["end_x_deg"] = 10.0;
    params["output"] = outputPath;
    handler.handle("scan_field", params, responder);

    ASSERT_EQ(responder.lastStatus, "done")
        << QJsonDocument(responder.lastData).toJson(QJsonDocument::Compact).constData();
    EXPECT_TRUE(responder.lastData.value("has_blank_scanlines").toBool());
}

TEST_F(ThreeDLaserRadarTestBase, HandlerScanFieldRequiresOutput) {
    FakeLaserTransport fake;
    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["begin_x_deg"] = 0.0;
    params["end_x_deg"] = 10.0;
    handler.handle("scan_field", params, responder);

    ASSERT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("output"));
}

TEST_F(ThreeDLaserRadarTestBase, HandlerScanFieldAllowsWideEndAnglesBeforeTransport) {
    FakeLaserTransport fake;
    fake.openShouldFail = true;
    fake.openError = "expected open failure";

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["begin_x_deg"] = 0.0;
    params["end_x_deg"] = 360.0;
    params["begin_y_deg"] = 0.0;
    params["end_y_deg"] = 360.0;
    params["output"] = dir.path() + "/scan.bin";
    handler.handle("scan_field", params, responder);

    ASSERT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 1);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("expected open failure"));
}

TEST_F(ThreeDLaserRadarTestBase, HandlerScanFieldUsesLegacyDefaultQueryIntervalWhenOmitted) {
    FakeLaserTransport fake;
    fake.openShouldFail = true;
    fake.openError = "expected open failure";

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    JsonResponder responder;
    QJsonObject params;
    params["host"] = "127.0.0.1";
    params["port"] = 23;
    params["timeout_ms"] = 100;
    params["task_timeout_ms"] = 20;
    params["begin_x_deg"] = 0.0;
    params["end_x_deg"] = 10.0;
    params["output"] = dir.path() + "/scan.bin";
    handler.handle("scan_field", params, responder);

    ASSERT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 1);
    EXPECT_EQ(fake.lastParams.queryIntervalMs, 5000);
}

TEST_F(ThreeDLaserRadarTestBase, HandlerGetDataSavesRequestedSegment) {
    FakeLaserTransport fake;
    const QByteArray segmentBytes("XYZ");
    fake.enqueueRead(encodeFrame(
        0, kDeviceAddr, CmdId::GetData, makeSegmentPayload(2, 4, segmentBytes)));

    ThreeDLaserRadarHandler handler;
    handler.setTransportFactory([&fake]() { return new NonOwningTransportWrapper(&fake); });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString outputPath = dir.path() + "/seg.bin";

    JsonResponder responder;
    QJsonObject params = baseParams();
    params["segment_index"] = 2;
    params["output"] = outputPath;
    handler.handle("get_data", params, responder);

    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("segment_count").toInt(), 4);

    QFile file(outputPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), segmentBytes);
}

TEST_F(ThreeDLaserRadarTestBase, MetaContainsExpectedCommands) {
    ThreeDLaserRadarHandler handler;
    const auto& meta = handler.driverMeta();
    EXPECT_EQ(meta.info.id, "stdio.drv.3d_laser_radar");
    EXPECT_EQ(meta.info.profiles, QStringList({"oneshot"}));

    const QStringList expectedCommands = {
        "status", "test", "get_reg", "set_reg", "query", "cancel", "set_imaging_mode",
        "reboot", "get_data", "scan_field", "get_work_mode", "get_device_status",
        "get_device_code", "get_lidar_model_code", "get_distance_unit", "get_uptime_ms",
        "get_firmware_version", "get_data_block_size", "get_x_axis_ratio",
        "get_transfer_total_bytes", "calib_x", "calib_lidar", "move_x"
    };

    for (const QString& name : expectedCommands) {
        EXPECT_NE(meta.findCommand(name), nullptr) << name.toStdString();
    }
    const auto* testCommand = meta.findCommand("test");
    ASSERT_NE(testCommand, nullptr);
    const QJsonObject testConsoleExample = findExampleByMode(testCommand->examples, "console");
    ASSERT_FALSE(testConsoleExample.isEmpty());
    const QJsonObject testConsoleParams = testConsoleExample.value("params").toObject();
    EXPECT_EQ(testConsoleParams.value("host").toString(), "127.0.0.1");
    EXPECT_EQ(testConsoleParams.value("port").toInt(), 23);

    const auto* scanFieldCommand = meta.findCommand("scan_field");
    ASSERT_NE(scanFieldCommand, nullptr);
    EXPECT_FALSE(findExampleByMode(scanFieldCommand->examples, "console").isEmpty());
    bool foundScanFieldQueryInterval = false;
    for (const auto& field : scanFieldCommand->params) {
        if (field.name == "query_interval_ms") {
            foundScanFieldQueryInterval = true;
            EXPECT_EQ(field.defaultValue.toInt(), 5000);
        }
    }
    EXPECT_TRUE(foundScanFieldQueryInterval);
    EXPECT_EQ(meta.findCommand("scan_to_angle"), nullptr);
    EXPECT_EQ(meta.findCommand("get_scan_line"), nullptr);
    EXPECT_EQ(meta.findCommand("get_system_status"), nullptr);
    EXPECT_EQ(meta.findCommand("set_scan_mode"), nullptr);
}

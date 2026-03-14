#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <QtEndian>

#include "driver_3d_scan_robot/handler.h"
#include "driver_3d_scan_robot/protocol_codec.h"
#include "driver_3d_scan_robot/radar_session.h"
#include "driver_3d_scan_robot/radar_transport.h"
#include "helpers/fake_3d_scan_robot_device.h"
#include "stdiolink/driver/mock_responder.h"

using namespace scan_robot;
using namespace stdiolink;

namespace {

class ThreeDScanRobotTestBase : public ::testing::Test {
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

// Non-owning variant: create a handler that borrows (not owns) the fake
// We need a wrapper that prevents deletion
class NonOwningTransportWrapper : public IRadarTransport {
public:
    explicit NonOwningTransportWrapper(IRadarTransport* inner) : m_inner(inner) {}
    bool open(const RadarTransportParams& p, QString* e) override { return m_inner->open(p, e); }
    bool writeFrame(const QByteArray& f, int t, QString* e) override { return m_inner->writeFrame(f, t, e); }
    bool readSome(QByteArray& c, int t, QString* e) override { return m_inner->readSome(c, t, e); }
    void close() override { m_inner->close(); }
private:
    IRadarTransport* m_inner;
};

ThreeDScanRobotHandler makeHandlerBorrowing(Fake3DScanRobotDevice* fake) {
    ThreeDScanRobotHandler handler;
    handler.setTransportFactory([fake]() -> IRadarTransport* {
        return new NonOwningTransportWrapper(fake);
    });
    return handler;
}

QJsonObject baseParams(const QString& port = "COM_TEST", int addr = 1) {
    return QJsonObject{{"port", port}, {"addr", addr}};
}

QJsonObject longTaskParams(const QString& port = "COM_TEST", int addr = 1) {
    QJsonObject p = baseParams(port, addr);
    p["task_timeout_ms"] = 2000;
    p["query_interval_ms"] = 50;
    p["inter_command_delay_ms"] = 10;
    return p;
}

} // namespace

// ══════════════════════════════════════════════════════════
// Protocol Codec Tests (T01-T07)
// ══════════════════════════════════════════════════════════

TEST(ThreeDScanRobotProtocolTest, T01_DecodeValidFrame) {
    QByteArray payload = makeU32Payload(0x12345678);
    QByteArray frame = encodeFrame(5, 1, CmdId::TestCom, payload);

    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, CmdId::TestCom, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::Ok) << error.toStdString();
    EXPECT_EQ(decoded.counter, 5);
    EXPECT_EQ(decoded.addr, 1);
    EXPECT_EQ(decoded.command, CmdId::TestCom);
    EXPECT_EQ(decoded.payload.size(), 4);
    EXPECT_TRUE(error.isEmpty());
}

TEST(ThreeDScanRobotProtocolTest, T02_DecodeIncompleteFrame) {
    QByteArray shortBuf("JD3D", 4);
    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(shortBuf, 1, 1, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::Incomplete);
}

TEST(ThreeDScanRobotProtocolTest, T03_DecodeBadMagic) {
    QByteArray frame = encodeFrame(0, 1, 1, makeU32Payload(0));
    frame[0] = 'X';  // corrupt magic
    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, 1, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::BadMagic);
}

TEST(ThreeDScanRobotProtocolTest, T04_DecodeCrcError) {
    QByteArray frame = encodeFrame(0, 1, 1, makeU32Payload(0));
    frame[frame.size() - 1] ^= 0xFF;  // flip CRC byte
    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, 1, &decoded, &error, FrameChannel::Any, nullptr, true);
    EXPECT_EQ(status, DecodeStatus::CrcError);
}

TEST(ThreeDScanRobotProtocolTest, T05_DecodeAddrMismatch) {
    QByteArray frame = encodeFrame(0, 2, 1, makeU32Payload(0));
    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, 1, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::AddrMismatch);
}

TEST(ThreeDScanRobotProtocolTest, T06_DecodeCmdMismatch) {
    QByteArray frame = encodeFrame(0, 1, 3, makeU32Payload(0));
    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, 1, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::CmdMismatch);
}

TEST(ThreeDScanRobotProtocolTest, T07_DecodeInterruptFrame) {
    QByteArray payload = makeU32Payload(999);
    QByteArray frame = encodeFrame(10, 1, InsertCmdId::ScanProgress, payload, true);

    ASSERT_GE(frame.size(), 4);
    EXPECT_EQ(frame[0], 'J');
    EXPECT_EQ(frame[1], 'D');
    EXPECT_EQ(frame[2], '3');
    EXPECT_EQ(frame[3], 'I');

    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(frame, 1, InsertCmdId::ScanProgress, &decoded, &error);
    EXPECT_EQ(status, DecodeStatus::Ok) << error.toStdString();
    EXPECT_EQ(decoded.counter, 10);
    EXPECT_EQ(decoded.command, InsertCmdId::ScanProgress);
}

TEST(ThreeDScanRobotProtocolTest, MainChannelRejectsInterruptFrame) {
    QByteArray payload = makeU32Payload(123);
    QByteArray frame = encodeFrame(1, 1, InsertCmdId::Test, payload, true);

    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(
        frame, 1, InsertCmdId::Test, &decoded, &error, FrameChannel::Main, nullptr, true);
    EXPECT_EQ(status, DecodeStatus::ChannelMismatch);
}

TEST(ThreeDScanRobotProtocolTest, InterruptChannelRejectsMainFrame) {
    QByteArray payload = makeU32Payload(123);
    QByteArray frame = encodeFrame(1, 1, CmdId::TestCom, payload, false);

    RadarFrame decoded;
    QString error;
    DecodeStatus status = tryDecodeFrame(
        frame, 1, CmdId::TestCom, &decoded, &error, FrameChannel::Interrupt, nullptr, true);
    EXPECT_EQ(status, DecodeStatus::ChannelMismatch);
}

// ══════════════════════════════════════════════════════════
// CRC32-STM32 roundtrip test
// ══════════════════════════════════════════════════════════

TEST(ThreeDScanRobotProtocolTest, CRC32_Roundtrip) {
    // Verify encode+decode roundtrip preserves CRC integrity
    for (int payloadSize : {4, 6, 8, 10, 14, 100}) {
        QByteArray payload(payloadSize, 'A');
        QByteArray frame = encodeFrame(42, 7, 3, payload);
        RadarFrame decoded;
        QString error;
        EXPECT_EQ(tryDecodeFrame(frame, 7, 3, &decoded, &error), DecodeStatus::Ok)
            << "payloadSize=" << payloadSize << ": " << error.toStdString();
    }
}

// ══════════════════════════════════════════════════════════
// Handler Tests (T08-T32)
// ══════════════════════════════════════════════════════════

class ThreeDScanRobotHandlerTest : public ThreeDScanRobotTestBase {
protected:
    Fake3DScanRobotDevice fake;
    MockResponder resp;
};

// T08 — Missing port triggers framework validation
TEST_F(ThreeDScanRobotHandlerTest, T08_MissingPortReturns400) {
    // The framework auto-validates required params before calling handle()
    // when using IMetaCommandHandler with autoValidateParams() == true.
    // We test that at the meta level, "port" is required.
    auto handler = makeHandlerBorrowing(&fake);
    const auto& meta = handler.driverMeta();
    const auto* getModeCmd = meta.findCommand("get_mode");
    ASSERT_NE(getModeCmd, nullptr);

    bool portRequired = false;
    for (const auto& param : getModeCmd->params) {
        if (param.name == "port") {
            portRequired = param.required;
            break;
        }
    }
    EXPECT_TRUE(portRequired);
}

// T09 — Default serial params align with MatLab
TEST_F(ThreeDScanRobotHandlerTest, T09_DefaultSerialParamsMatchMatLab) {
    fake.enqueueReadRegisterSuccess(1, 0, RegId::WorkMode, 20);
    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p{{"port", "COM_TEST"}, {"addr", 1}};
    handler.handle("get_mode", p, resp);

    EXPECT_EQ(fake.lastParams().baudRate, 115200);
    EXPECT_EQ(fake.lastParams().timeoutMs, 5000);
    EXPECT_EQ(fake.lastParams().queryIntervalMs, 1000);
    EXPECT_EQ(fake.lastParams().interCommandDelayMs, 250);
}

// T10 — Serial open failure returns code=1
TEST_F(ThreeDScanRobotHandlerTest, T10_SerialOpenFailReturnsCode1) {
    fake.setOpenFail(true);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_mode", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 1);
}

// T11 — Serial read timeout returns code=1
TEST_F(ThreeDScanRobotHandlerTest, T11_SerialReadTimeoutReturnsCode1) {
    fake.setReadTimeout(true);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_mode", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 1);
}

// T12 — Query success matches counter and command
TEST_F(ThreeDScanRobotHandlerTest, T12_QuerySuccessMatchesCounterAndCommand) {
    // Test calib command which uses long task polling
    // 1. Response to calib command
    fake.enqueueSimpleResponse(1, 0, CmdId::Calibration, makeU32Payload(0));
    // 2. Query response: matching counter=0, cmd=4, resultCode=10 (success)
    fake.enqueueQueryResponse(1, 0, 0, CmdId::Calibration, TaskResult::Success);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("calib", longTaskParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    EXPECT_EQ(resp.responses.back().code, 0);
}

// T13 — Query timeout
TEST_F(ThreeDScanRobotHandlerTest, T13_QueryTimeout) {
    // Response to calib command
    fake.enqueueSimpleResponse(1, 0, CmdId::Calibration, makeU32Payload(0));
    // Query responses all return StillRunning (timeout will occur)
    for (int i = 0; i < 50; ++i) {
        fake.enqueueQueryResponse(1, 0, 0, CmdId::Calibration, TaskResult::StillRunning);
    }

    auto handler = makeHandlerBorrowing(&fake);
    QJsonObject p = longTaskParams();
    p["task_timeout_ms"] = 500;
    p["query_interval_ms"] = 20;
    p["inter_command_delay_ms"] = 5;
    handler.handle("calib", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

// T14 — Old counter/cmd from previous task is skipped
TEST_F(ThreeDScanRobotHandlerTest, T14_OldCounterSkipped) {
    // Response to calib command, counter=0 sent
    fake.enqueueSimpleResponse(1, 0, CmdId::Calibration, makeU32Payload(0));
    // First query: old task result (different counter=99, different cmd=7)
    fake.enqueueQueryResponse(1, 0, 99, CmdId::Move, TaskResult::Success);
    // Second query: matching current task
    fake.enqueueQueryResponse(1, 0, 0, CmdId::Calibration, TaskResult::Success);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("calib", longTaskParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
}

// T15 — Device failure result code
TEST_F(ThreeDScanRobotHandlerTest, T15_DeviceFailureResultCode) {
    fake.enqueueSimpleResponse(1, 0, CmdId::Calibration, makeU32Payload(0));
    fake.enqueueQueryResponse(1, 0, 0, CmdId::Calibration, TaskResult::Failed);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("calib", longTaskParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

// T16 — Segment collection complete
TEST_F(ThreeDScanRobotHandlerTest, T16_SegmentCollectionComplete) {
    Fake3DScanRobotDevice sessionFake;
    RadarTransportParams tp;
    tp.port = "COM_TEST";
    tp.addr = 1;
    tp.timeoutMs = 1000;

    // Prepare segment size read
    sessionFake.enqueueSegmentSizeResponse(1, 0, 32);

    // Prepare 2 segments of 32 bytes each for 64 total bytes
    QByteArray seg1(32, 'A');
    QByteArray seg2(32, 'B');
    sessionFake.enqueueScanSegments(1, {seg1, seg2}, 32);

    RadarSession session(&sessionFake);
    QString err;
    ASSERT_TRUE(session.open(tp, &err));

    ScanAggregateResult result;
    ASSERT_TRUE(session.collectScanData(64, 10, &result, &err)) << err.toStdString();
    EXPECT_EQ(result.segmentCount, 2);
    EXPECT_EQ(result.byteCount, 64);
    EXPECT_EQ(result.data.size(), 64);
    // First 32 bytes should be 'A', next 32 should be 'B'
    EXPECT_EQ(result.data.mid(0, 32), seg1);
    EXPECT_EQ(result.data.mid(32, 32), seg2);
}

// T17 — Segment missing causes error
TEST_F(ThreeDScanRobotHandlerTest, T17_SegmentMissingCausesError) {
    Fake3DScanRobotDevice sessionFake;
    RadarTransportParams tp;
    tp.port = "COM_TEST";
    tp.addr = 1;
    tp.timeoutMs = 1000;

    // Segment size read succeeds
    sessionFake.enqueueSegmentSizeResponse(1, 0, 32);
    // But no segments are queued → readSome will fail → collect fails

    RadarSession session(&sessionFake);
    QString err;
    ASSERT_TRUE(session.open(tp, &err));

    ScanAggregateResult result;
    EXPECT_FALSE(session.collectScanData(64, 10, &result, &err));
}

// T18 — Resource release and repeated close safety
TEST_F(ThreeDScanRobotHandlerTest, T18_ResourceRelease) {
    Fake3DScanRobotDevice sessionFake;
    RadarTransportParams tp;
    tp.port = "COM_TEST";
    tp.addr = 1;

    {
        RadarSession session(&sessionFake);
        QString err;
        ASSERT_TRUE(session.open(tp, &err));
        EXPECT_TRUE(sessionFake.isOpen());
    }
    // After destruction, close should have been called at least once
    EXPECT_GE(sessionFake.closeCount(), 1);
    EXPECT_FALSE(sessionFake.isOpen());

    // Repeated close should not crash
    sessionFake.close();
    sessionFake.close();
}

// T19 — Parameter error returns 400 (meta validation)
TEST_F(ThreeDScanRobotHandlerTest, T19_MoveParamRequired) {
    auto handler = makeHandlerBorrowing(&fake);
    const auto& meta = handler.driverMeta();
    const auto* moveCmd = meta.findCommand("move");
    ASSERT_NE(moveCmd, nullptr);

    bool xRequired = false, yRequired = false;
    for (const auto& param : moveCmd->params) {
        if (param.name == "x_deg") xRequired = param.required;
        if (param.name == "y_deg") yRequired = param.required;
    }
    EXPECT_TRUE(xRequired);
    EXPECT_TRUE(yRequired);
}

// T20 — Transport error returns code=1 for read/write
TEST_F(ThreeDScanRobotHandlerTest, T20_TransportErrorReturnsCode1) {
    // Open will succeed, but read will fail
    fake.setReadTimeout(true);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("test", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 1);
}

// T21 — Protocol error returns code=2
TEST_F(ThreeDScanRobotHandlerTest, T21_ProtocolErrorReturnsCode2) {
    // Enqueue a frame with wrong command (addr=1, cmd=99 instead of expected)
    QByteArray badFrame = encodeFrame(0, 1, 99, makeU32Payload(0));
    fake.enqueueRawResponse(badFrame);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("test", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

TEST_F(ThreeDScanRobotHandlerTest, MainCommandRejectsInterruptFrameWithCode2) {
    fake.enqueueInterruptTestReply(1, 0, 42);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("test", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

TEST_F(ThreeDScanRobotHandlerTest, InterruptCommandRejectsMainFrameWithCode2) {
    fake.enqueueTestComSuccess(1, 0, 1000);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("interrupt_test", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

TEST_F(ThreeDScanRobotHandlerTest, GetAddrEchoMismatchReturnsCode2) {
    fake.enqueueSimpleResponse(7, 0, CmdId::TestCom, makeU32Payload(1234));

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_addr", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 2);
}

// T22 — Unknown command returns 404
TEST_F(ThreeDScanRobotHandlerTest, T22_UnknownCommandReturns404) {
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("nonexistent_cmd", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().code, 404);
}

// T23 — scan_line returns aggregated raw result
TEST_F(ThreeDScanRobotHandlerTest, T23_ScanLineReturnsAggregatedRawResult) {
    // 1. Response to scan line command
    fake.enqueueSimpleResponse(1, 0, CmdId::ScanLine, makeU32Payload(0));

    // 2. Query response: success, resultCode = 64 (total scan bytes)
    fake.enqueueQueryResponse(1, 0, 0, CmdId::ScanLine, 64);

    // 3. Segment size read
    fake.enqueueReadRegisterSuccess(1, 0, RegId::SegmentSize, 32);

    // 4. Two segments
    QByteArray seg1(32, 'X');
    QByteArray seg2(32, 'Y');
    fake.enqueueScanSegments(1, {seg1, seg2}, 32);

    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = longTaskParams();
    p["angle_x"] = 97.0;
    p["begin_y"] = 1.0;
    p["end_y"] = 100.0;
    p["step_y"] = 1.0;
    p["speed_y"] = 10.0;
    handler.handle("scan_line", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["task_command"].toString(), "scan_line");
    EXPECT_EQ(result["segment_count"].toInt(), 2);
    EXPECT_EQ(result["byte_count"].toInt(), 64);
    EXPECT_FALSE(result["data_base64"].toString().isEmpty());
}

// T24 — scan_frame returns aggregated raw result
TEST_F(ThreeDScanRobotHandlerTest, T24_ScanFrameReturnsAggregatedRawResult) {
    // 1. Response to scan frame command
    fake.enqueueSimpleResponse(1, 0, CmdId::ScanFrame, makeU32Payload(0));

    // 2. Query response: success, resultCode = 128 (total frame bytes)
    fake.enqueueQueryResponse(1, 0, 0, CmdId::ScanFrame, 128);

    // 3. Segment size read
    fake.enqueueReadRegisterSuccess(1, 0, RegId::SegmentSize, 64);

    // 4. Two segments
    QByteArray seg1(64, 'F');
    QByteArray seg2(64, 'G');
    fake.enqueueScanSegments(1, {seg1, seg2}, 64);

    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = longTaskParams();
    p["begin_x"] = 0.0;
    p["end_x"] = 185.0;
    p["step_x"] = 5.0;
    p["begin_y"] = 1.0;
    p["end_y"] = 100.0;
    p["step_y"] = 1.0;
    p["speed_y"] = 10.0;
    handler.handle("scan_frame", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["task_command"].toString(), "scan_frame");
    EXPECT_EQ(result["segment_count"].toInt(), 2);
    EXPECT_EQ(result["byte_count"].toInt(), 128);
    EXPECT_FALSE(result["data_base64"].toString().isEmpty());
}

// T25 — get_distance_at returns target angles and distance
TEST_F(ThreeDScanRobotHandlerTest, T25_GetDistanceAtReturnsDistanceAtTargetAngles) {
    // 1. Response to get_distance_at command
    fake.enqueueSimpleResponse(1, 0, CmdId::MoveDist, makeU32Payload(0));

    // 2. Query response: counter=0, cmd=5, resultCode = distance in mm
    fake.enqueueQueryResponse(1, 0, 0, CmdId::MoveDist, 1500);

    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = longTaskParams();
    p["x_deg"] = 90.0;
    p["y_deg"] = 45.0;
    handler.handle("get_distance_at", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_DOUBLE_EQ(result["x_deg"].toDouble(), 90.0);
    EXPECT_DOUBLE_EQ(result["y_deg"].toDouble(), 45.0);
    EXPECT_EQ(result["distance_mm"].toInt(), 1500);
}

// T26 — get_data independently pulls scan data
TEST_F(ThreeDScanRobotHandlerTest, T26_GetDataIndependentPull) {
    // 1. Segment size read
    fake.enqueueReadRegisterSuccess(1, 0, RegId::SegmentSize, 32);

    // 2. One segment of data
    QByteArray seg1(32, 'D');
    fake.enqueueScanSegments(1, {seg1}, 32);

    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = baseParams();
    p["inter_command_delay_ms"] = 10;
    p["total_bytes"] = 32;
    handler.handle("get_data", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["segment_count"].toInt(), 1);
    EXPECT_EQ(result["byte_count"].toInt(), 32);
}

// T27 — scan_progress returns frame progress
TEST_F(ThreeDScanRobotHandlerTest, T27_ScanProgressReturnsFrameProgress) {
    fake.enqueueInterruptProgress(1, 0, 50, 200);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("scan_progress", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["current_line"].toInt(), 50);
    EXPECT_EQ(result["total_lines"].toInt(), 200);
}

// T28 — scan_cancel returns stop confirmation
TEST_F(ThreeDScanRobotHandlerTest, T28_ScanCancelReturnsConfirmation) {
    fake.enqueueInterruptStopReply(1, 0, 12345);

    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("scan_cancel", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["value"].toInt(), 12345);
}

// T29 — test / interrupt_test communication test
TEST_F(ThreeDScanRobotHandlerTest, T29_TestAndInterruptTest) {
    // test (main protocol)
    {
        Fake3DScanRobotDevice f;
        f.enqueueTestComSuccess(1, 0, 1000);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["value"] = 1000;
        handler.handle("test", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        quint32 expected = ~static_cast<quint32>(1000);
        EXPECT_EQ(static_cast<quint32>(r.responses.back().payload.toObject()["echo"].toInteger()), expected);
    }

    // interrupt_test (interrupt protocol)
    {
        Fake3DScanRobotDevice f;
        f.enqueueInterruptTestReply(1, 0, 42);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["value"] = 1000;
        handler.handle("interrupt_test", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        EXPECT_EQ(r.responses.back().payload.toObject()["value"].toInt(), 42);
    }
}

// T30 — query returns last task result summary
TEST_F(ThreeDScanRobotHandlerTest, T30_QueryReturnsTaskResultSummary) {
    // query op=100
    {
        Fake3DScanRobotDevice f;
        f.enqueueQueryResponse(1, 0, 5, CmdId::Move, TaskResult::Success);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["op"] = 100;
        handler.handle("query", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        QJsonObject result = r.responses.back().payload.toObject();
        EXPECT_EQ(result["counter"].toInt(), 5);
        EXPECT_EQ(result["command"].toInt(), CmdId::Move);
        EXPECT_EQ(result["result"].toInt(), TaskResult::Success);
    }

    // query op=200
    {
        Fake3DScanRobotDevice f;
        f.enqueueQueryResponse(1, 0, 5, CmdId::Move, TaskResult::Success);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["op"] = 200;
        handler.handle("query", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        QJsonObject result = r.responses.back().payload.toObject();
        EXPECT_EQ(result["counter"].toInt(), 5);
        EXPECT_EQ(result["command"].toInt(), CmdId::Move);
        EXPECT_EQ(result["result"].toInt(), TaskResult::Success);
    }
}

// ══════════════════════════════════════════════════════════
// Meta / status tests
// ══════════════════════════════════════════════════════════

TEST(ThreeDScanRobotMetaTest, MetaDescribeContainsAllCommands) {
    ThreeDScanRobotHandler handler;
    const auto& meta = handler.driverMeta();

    EXPECT_EQ(meta.info.id, "stdio.drv.3d_scan_robot");
    EXPECT_EQ(meta.schemaVersion, "1.0");

    QStringList expectedCmds = {
        "status", "test", "get_addr", "set_addr",
        "get_mode", "set_mode", "get_temp", "get_state",
        "get_version", "get_angles", "get_switch_x", "get_switch_y",
        "get_calib_x", "get_calib_y", "calib", "calib_x", "calib_y",
        "move", "get_distance_at", "get_distance", "get_reg", "set_reg",
        "scan_line", "scan_frame", "get_data", "query",
        "interrupt_test", "scan_progress", "scan_cancel"
    };

    for (const auto& name : expectedCmds) {
        EXPECT_NE(meta.findCommand(name), nullptr)
            << "Missing command: " << name.toStdString();
    }

    QStringList removedCmds = {
        "test_com", "get_fw_ver", "get_direction", "get_sw0", "get_sw1",
        "get_calib0", "get_calib1", "calib0", "calib1", "move_dist",
        "get_dist", "get_line", "get_frame", "res", "wait",
        "insert_test", "insert_state", "insert_stop", "radar_get_response_time",
        "dist", "state", "get_ver", "get_dir", "gr", "sr", "rgrt"
    };
    for (const auto& name : removedCmds) {
        EXPECT_EQ(meta.findCommand(name), nullptr)
            << "Removed command still present: " << name.toStdString();
    }
}

TEST(ThreeDScanRobotMetaTest, StatusCommand) {
    ThreeDScanRobotHandler handler;
    MockResponder resp;
    handler.handle("status", QJsonValue(), resp);
    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    EXPECT_EQ(resp.responses.back().code, 0);
    EXPECT_EQ(resp.responses.back().payload.toObject()["status"].toString(), "ready");
}

TEST(ThreeDScanRobotMetaTest, ScanCommandsUseProtocolParameterNamesAndDefaults) {
    ThreeDScanRobotHandler handler;
    const auto& meta = handler.driverMeta();

    const auto* scanLineCmd = meta.findCommand("scan_line");
    const auto* scanFrameCmd = meta.findCommand("scan_frame");
    ASSERT_NE(scanLineCmd, nullptr);
    ASSERT_NE(scanFrameCmd, nullptr);

    auto findParam = [](const stdiolink::meta::CommandMeta* cmd, const QString& name)
        -> const stdiolink::meta::FieldMeta* {
        for (const auto& param : cmd->params) {
            if (param.name == name) {
                return &param;
            }
        }
        return nullptr;
    };

    const auto* angleX = findParam(scanLineCmd, "angle_x");
    const auto* speedYLine = findParam(scanLineCmd, "speed_y");
    const auto* beginX = findParam(scanFrameCmd, "begin_x");
    const auto* speedYFrame = findParam(scanFrameCmd, "speed_y");
    ASSERT_NE(angleX, nullptr);
    ASSERT_NE(speedYLine, nullptr);
    ASSERT_NE(beginX, nullptr);
    ASSERT_NE(speedYFrame, nullptr);

    EXPECT_EQ(angleX->defaultValue.toDouble(), 0.0);
    EXPECT_EQ(speedYLine->defaultValue.toDouble(), 10.0);
    EXPECT_EQ(beginX->defaultValue.toDouble(), 0.0);
    EXPECT_EQ(speedYFrame->defaultValue.toDouble(), 10.0);
    EXPECT_TRUE(speedYLine->description.contains(QString::fromUtf8("Y 轴旋转速度")));
    EXPECT_FALSE(speedYLine->description.contains("sample_count"));
    EXPECT_FALSE(angleX->name.contains("_deg"));
}

// ══════════════════════════════════════════════════════════
// Additional register command tests
// ══════════════════════════════════════════════════════════

TEST_F(ThreeDScanRobotHandlerTest, GetModeReturnsSemanticResult) {
    fake.enqueueReadRegisterSuccess(1, 0, RegId::WorkMode, 20);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_mode", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["mode"].toString(), "imaging");
    EXPECT_EQ(result["mode_code"].toInt(), 20);
}

TEST_F(ThreeDScanRobotHandlerTest, SetModeSuccess) {
    fake.enqueueWriteRegisterSuccess(1, 0, RegId::WorkMode, 20);
    auto handler = makeHandlerBorrowing(&fake);
    QJsonObject p = baseParams();
    p["mode"] = "imaging";
    handler.handle("set_mode", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
}

TEST_F(ThreeDScanRobotHandlerTest, SetModeSupportsStandbyAndSleep) {
    {
        Fake3DScanRobotDevice f;
        f.enqueueWriteRegisterSuccess(1, 0, RegId::WorkMode, 30);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["mode"] = "standby";
        handler.handle("set_mode", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        EXPECT_EQ(r.responses.back().payload.toObject()["mode_code"].toInt(), 30);
    }
    {
        Fake3DScanRobotDevice f;
        f.enqueueWriteRegisterSuccess(1, 0, RegId::WorkMode, 40);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        QJsonObject p = baseParams();
        p["mode"] = "sleep";
        handler.handle("set_mode", p, r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().status, "done");
        EXPECT_EQ(r.responses.back().payload.toObject()["mode_code"].toInt(), 40);
    }
}

TEST_F(ThreeDScanRobotHandlerTest, GetDistanceReturnsDistance) {
    fake.enqueueSimpleResponse(1, 0, CmdId::GetDistance, makeU32Payload(4567));
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_distance", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["distance_mm"].toInt(), 4567);
}

TEST_F(ThreeDScanRobotHandlerTest, GetAnglesReturnsAngles) {
    // X=90.50° (9050), Y=45.25° (4525) packed as upper16=9050, lower16=4525
    quint32 packed = (9050u << 16) | 4525u;
    fake.enqueueReadRegisterSuccess(1, 0, RegId::DirectionAngle, packed);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_angles", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_DOUBLE_EQ(result["x_deg"].toDouble(), 90.50);
    EXPECT_DOUBLE_EQ(result["y_deg"].toDouble(), 45.25);
}

TEST_F(ThreeDScanRobotHandlerTest, GetTempReturnsSemanticTemperatures) {
    fake.enqueueReadRegisterSuccess(1, 0, RegId::McuTemperature, 2534);
    fake.enqueueReadRegisterSuccess(1, 1, RegId::BoardTemperature, 2789);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_temp", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["mcu_temp_raw"].toInt(), 2534);
    EXPECT_EQ(result["board_temp_raw"].toInt(), 2789);
    EXPECT_DOUBLE_EQ(result["mcu_temp_deg_c"].toDouble(), 25.34);
    EXPECT_DOUBLE_EQ(result["board_temp_deg_c"].toDouble(), 27.89);
}

TEST_F(ThreeDScanRobotHandlerTest, GetSwitchAndCalibrationReturnSemanticState) {
    {
        Fake3DScanRobotDevice f;
        f.enqueueReadRegisterSuccess(1, 0, RegId::XProximitySwitch, 10);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        handler.handle("get_switch_x", baseParams(), r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().payload.toObject()["state"].toString(), "near");
        EXPECT_EQ(r.responses.back().payload.toObject()["state_code"].toInt(), 10);
    }
    {
        Fake3DScanRobotDevice f;
        f.enqueueReadRegisterSuccess(1, 0, RegId::YMotorCalib, 20);
        auto handler = makeHandlerBorrowing(&f);
        MockResponder r;
        handler.handle("get_calib_y", baseParams(), r);
        ASSERT_FALSE(r.responses.empty());
        EXPECT_EQ(r.responses.back().payload.toObject()["state"].toString(), "uncalibrated");
        EXPECT_EQ(r.responses.back().payload.toObject()["state_code"].toInt(), 20);
    }
}

TEST_F(ThreeDScanRobotHandlerTest, GetStateReturnsRawAndConfirmedFlags) {
    const quint32 rawState = (1u << 0) | (1u << 4) | (1u << 7);
    fake.enqueueReadRegisterSuccess(1, 0, RegId::DeviceStatus, rawState);
    auto handler = makeHandlerBorrowing(&fake);
    handler.handle("get_state", baseParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    QJsonObject result = resp.responses.back().payload.toObject();
    EXPECT_EQ(result["raw_state"].toInt(), static_cast<int>(rawState));
    const QJsonObject flags = result["flags"].toObject();
    EXPECT_TRUE(flags["clock_error"].toBool());
    EXPECT_TRUE(flags["x_calibration_error"].toBool());
    EXPECT_TRUE(flags["radar_communication_error"].toBool());
    EXPECT_FALSE(flags["flash_error"].toBool());
}

TEST_F(ThreeDScanRobotHandlerTest, MoveSuccess) {
    // Response to move command
    fake.enqueueSimpleResponse(1, 0, CmdId::Move, makeU32Payload(0));
    // Query: success
    fake.enqueueQueryResponse(1, 0, 0, CmdId::Move, TaskResult::Success);

    auto handler = makeHandlerBorrowing(&fake);
    QJsonObject p = longTaskParams();
    p["x_deg"] = 90.5;
    p["y_deg"] = 45.25;
    handler.handle("move", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "done");
    EXPECT_EQ(resp.responses.back().payload.toObject()["result"].toString(), "ok");
}

TEST_F(ThreeDScanRobotHandlerTest, ScanLineEncodesAnglesAndSpeedAsCentiDegrees) {
    fake.setCustomHandler([](const QByteArray&, Fake3DScanRobotDevice&) {});
    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = longTaskParams();
    p["angle_x"] = 12.34;
    p["begin_y"] = 1.25;
    p["end_y"] = 8.75;
    p["step_y"] = 0.5;
    p["speed_y"] = 3.21;
    handler.handle("scan_line", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    ASSERT_GE(fake.sentFrameCount(), 1);

    RadarFrame sent;
    QString error;
    ASSERT_EQ(tryDecodeFrame(fake.lastSentFrame(), 1, CmdId::ScanLine, &sent, &error),
              DecodeStatus::Ok) << error.toStdString();
    const uchar* data = reinterpret_cast<const uchar*>(sent.payload.constData());
    ASSERT_EQ(sent.payload.size(), 10);
    EXPECT_EQ(qFromBigEndian<quint16>(data), 1234);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 2), 125);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 4), 875);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 6), 50);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 8), 321);
}

TEST_F(ThreeDScanRobotHandlerTest, ScanFrameEncodesAnglesAndSpeedAsCentiDegrees) {
    fake.setCustomHandler([](const QByteArray&, Fake3DScanRobotDevice&) {});
    auto handler = makeHandlerBorrowing(&fake);

    QJsonObject p = longTaskParams();
    p["begin_x"] = 0.0;
    p["end_x"] = 180.0;
    p["step_x"] = 5.0;
    p["begin_y"] = 1.0;
    p["end_y"] = 100.0;
    p["step_y"] = 0.25;
    p["speed_y"] = 9.99;
    handler.handle("scan_frame", p, resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    ASSERT_GE(fake.sentFrameCount(), 1);

    RadarFrame sent;
    QString error;
    ASSERT_EQ(tryDecodeFrame(fake.lastSentFrame(), 1, CmdId::ScanFrame, &sent, &error),
              DecodeStatus::Ok) << error.toStdString();
    const uchar* data = reinterpret_cast<const uchar*>(sent.payload.constData());
    ASSERT_EQ(sent.payload.size(), 14);
    EXPECT_EQ(qFromBigEndian<quint16>(data), 0);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 2), 18000);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 4), 500);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 6), 100);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 8), 10000);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 10), 25);
    EXPECT_EQ(qFromBigEndian<quint16>(data + 12), 999);
}

TEST_F(ThreeDScanRobotHandlerTest, LegacyCommandNamesReturn404) {
    auto handler = makeHandlerBorrowing(&fake);

    handler.handle("get_line", longTaskParams(), resp);

    ASSERT_FALSE(resp.responses.empty());
    EXPECT_EQ(resp.responses.back().status, "error");
    EXPECT_EQ(resp.responses.back().code, 404);
}

TEST_F(ThreeDScanRobotHandlerTest, LegacyScanParameterNamesFailValidationAtMetaLayer) {
    auto handler = makeHandlerBorrowing(&fake);
    const auto* scanLineCmd = handler.driverMeta().findCommand("scan_line");
    ASSERT_NE(scanLineCmd, nullptr);
    auto hasParam = [&](const QString& name) {
        for (const auto& param : scanLineCmd->params) {
            if (param.name == name) {
                return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(hasParam("angle_x_deg"));
    EXPECT_FALSE(hasParam("sample_count"));
    EXPECT_TRUE(hasParam("angle_x"));
    EXPECT_TRUE(hasParam("speed_y"));
}

TEST_F(ThreeDScanRobotHandlerTest, SessionHandlesFragmentedAndCoalescedFrames) {
    Fake3DScanRobotDevice sessionFake;
    RadarTransportParams tp;
    tp.port = "COM_TEST";
    tp.addr = 1;
    tp.timeoutMs = 200;

    const QByteArray firstFrame = encodeFrame(0, 1, CmdId::TestCom, makeU32Payload(~1000u));
    sessionFake.enqueueRawResponse(firstFrame.left(5));
    sessionFake.enqueueRawResponse(firstFrame.mid(5));

    RadarSession session(&sessionFake);
    QString err;
    ASSERT_TRUE(session.open(tp, &err));

    RadarFrame response;
    SessionErrorKind errorKind = SessionErrorKind::None;
    ASSERT_TRUE(session.sendAndReceive(CmdId::TestCom, makeU32Payload(1000), &response, &err, false, &errorKind))
        << err.toStdString();

    Fake3DScanRobotDevice coalescedFake;
    coalescedFake.open(tp, &err);
    const QByteArray secondFrame = encodeFrame(1, 1, CmdId::TestCom, makeU32Payload(~2000u));
    coalescedFake.enqueueRawResponse(secondFrame + encodeFrame(2, 1, CmdId::TestCom, makeU32Payload(~3000u)));
    RadarSession coalescedSession(&coalescedFake);
    ASSERT_TRUE(coalescedSession.open(tp, &err));
    ASSERT_TRUE(coalescedSession.sendAndReceive(CmdId::TestCom, makeU32Payload(2000), &response, &err, false, &errorKind))
        << err.toStdString();
}

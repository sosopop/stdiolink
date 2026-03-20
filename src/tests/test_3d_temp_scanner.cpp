#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <deque>
#include <functional>
#include <vector>

#include "driver_3d_temp_scanner/handler.h"
#include "driver_3d_temp_scanner/protocol_codec.h"
#include "driver_3d_temp_scanner/thermal_session.h"
#include "driver_3d_temp_scanner/thermal_transport.h"

using namespace temp_scanner;

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

class FakeThermalTransport : public IThermalTransport {
public:
    bool open(const ThermalTransportParams& params, QString* errorMessage) override {
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
                *errorMessage = "transport not open";
            }
            return false;
        }
        writes.push_back(frame);
        if (onWrite) {
            onWrite(frame);
        }
        return true;
    }

    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override {
        Q_UNUSED(timeoutMs);
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

    ThermalTransportParams lastParams;
    bool openShouldFail = false;
    QString openError = "open failed";
    QString readError = "read timeout";
    bool isOpen = false;
    int closeCount = 0;
    std::deque<QByteArray> queuedReads;
    std::vector<QByteArray> writes;
    std::function<void(const QByteArray&)> onWrite;
};

class NonOwningTransportWrapper : public IThermalTransport {
public:
    explicit NonOwningTransportWrapper(IThermalTransport* inner) : m_inner(inner) {}

    bool open(const ThermalTransportParams& params, QString* errorMessage) override {
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
    IThermalTransport* m_inner = nullptr;
};

QByteArray buildReadResponse(quint8 deviceAddr, quint8 functionCode, const QVector<quint16>& values) {
    QByteArray frame;
    frame.append(static_cast<char>(deviceAddr));
    frame.append(static_cast<char>(functionCode));
    const int byteCount = values.size() > 255 ? 0 : values.size() * 2;
    frame.append(static_cast<char>(byteCount));
    for (quint16 value : values) {
        frame.append(static_cast<char>((value >> 8) & 0xFF));
        frame.append(static_cast<char>(value & 0xFF));
    }
    const quint16 crc = calculateModbusCrc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray buildWriteResponse(quint8 deviceAddr, quint16 reg, quint16 value) {
    QByteArray frame;
    frame.append(static_cast<char>(deviceAddr));
    frame.append(static_cast<char>(static_cast<quint8>(FunctionCode::WriteSingleRegister)));
    frame.append(static_cast<char>((reg >> 8) & 0xFF));
    frame.append(static_cast<char>(reg & 0xFF));
    frame.append(static_cast<char>((value >> 8) & 0xFF));
    frame.append(static_cast<char>(value & 0xFF));
    const quint16 crc = calculateModbusCrc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

class ThreeDTempScannerTestBase : public ::testing::Test {
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

TEST(ThreeDTempScannerProtocolTest, ReadResponseSupportsByteCountZeroForLargeImage) {
    QVector<quint16> values;
    values.reserve(kImagePixelCount);
    for (int i = 0; i < kImagePixelCount; ++i) {
        values.append(static_cast<quint16>(i));
    }

    QVector<quint16> parsed;
    QString errorMessage;
    const ParseStatus status = parseReadHoldingRegistersResponse(
        buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), values),
        1,
        kImagePixelCount,
        &parsed,
        nullptr,
        &errorMessage);
    EXPECT_EQ(status, ParseStatus::Ok) << errorMessage.toStdString();
    EXPECT_EQ(parsed.size(), kImagePixelCount);
    EXPECT_EQ(parsed.front(), 0);
    EXPECT_EQ(parsed.back(), 767);
}

TEST(ThreeDTempScannerProtocolTest, ReadResponseDetectsCrcError) {
    QByteArray frame = buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{1});
    frame[frame.size() - 1] ^= 0xFF;

    QVector<quint16> parsed;
    QString errorMessage;
    const ParseStatus status = parseReadHoldingRegistersResponse(frame, 1, 1, &parsed, nullptr, &errorMessage);
    EXPECT_EQ(status, ParseStatus::CrcError);
}

TEST(ThreeDTempScannerProtocolTest, ReadResponseDetectsAddressMismatch) {
    QVector<quint16> parsed;
    QString errorMessage;
    const ParseStatus status = parseReadHoldingRegistersResponse(
        buildReadResponse(2, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{1}),
        1,
        1,
        &parsed,
        nullptr,
        &errorMessage);
    EXPECT_EQ(status, ParseStatus::AddressMismatch);
}

TEST(ThreeDTempScannerProtocolTest, ReadResponseDetectsFunctionMismatch) {
    QVector<quint16> parsed;
    QString errorMessage;
    const ParseStatus status = parseReadHoldingRegistersResponse(
        buildReadResponse(1, static_cast<quint8>(FunctionCode::WriteSingleRegister), QVector<quint16>{1}),
        1,
        1,
        &parsed,
        nullptr,
        &errorMessage);
    EXPECT_EQ(status, ParseStatus::FunctionMismatch);
}

TEST(ThreeDTempScannerProtocolTest, WriteResponseDetectsEchoMismatch) {
    QByteArray frame = buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue);
    frame[5] ^= 0x01;
    const quint16 crc = calculateModbusCrc16(frame.left(frame.size() - 2));
    frame[frame.size() - 2] = static_cast<char>(crc & 0xFF);
    frame[frame.size() - 1] = static_cast<char>((crc >> 8) & 0xFF);

    QString errorMessage;
    const ParseStatus status = parseWriteSingleRegisterResponse(
        frame, 1, kRegisterCaptureControl, kCaptureStartValue, nullptr, &errorMessage);
    EXPECT_EQ(status, ParseStatus::EchoMismatch);
    EXPECT_TRUE(errorMessage.contains("echo mismatch"));
}

TEST_F(ThreeDTempScannerTestBase, SessionCaptureSucceeds) {
    FakeThermalTransport transport;
    transport.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
    transport.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{kCaptureSuccessValue}));

    QVector<quint16> pixels;
    for (int i = 0; i < kImagePixelCount; ++i) {
        pixels.append(static_cast<quint16>(30000 + i));
    }
    transport.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), pixels));

    ThermalSession session(&transport);
    ThermalTransportParams params;
    params.portName = "COM_TEST";
    params.pollIntervalMs = 1;
    params.scanTimeoutMs = 50;
    QString errorMessage;
    ASSERT_TRUE(session.open(params, &errorMessage));

    CaptureResult result;
    ASSERT_TRUE(session.capture(&result, &errorMessage)) << errorMessage.toStdString();
    EXPECT_EQ(result.rawTemperatures.size(), kImagePixelCount);
    EXPECT_EQ(result.temperaturesDegC.size(), kImagePixelCount);
    EXPECT_EQ(result.width, 32);
    EXPECT_EQ(result.height, 24);
}

TEST_F(ThreeDTempScannerTestBase, SessionCaptureTimesOutWhenStateNeverCompletes) {
    FakeThermalTransport transport;
    transport.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
    for (int i = 0; i < 30; ++i) {
        transport.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{0}));
    }

    ThermalSession session(&transport);
    ThermalTransportParams params;
    params.portName = "COM_TEST";
    params.pollIntervalMs = 1;
    params.scanTimeoutMs = 10;
    QString errorMessage;
    ASSERT_TRUE(session.open(params, &errorMessage));

    CaptureResult result;
    EXPECT_FALSE(session.capture(&result, &errorMessage));
    EXPECT_TRUE(errorMessage.contains("timeout"));
}

TEST_F(ThreeDTempScannerTestBase, SessionCaptureReturnsFailureWhenDeviceReportsFailure) {
    FakeThermalTransport transport;
    transport.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
    transport.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{kCaptureFailureValue}));

    ThermalSession session(&transport);
    ThermalTransportParams params;
    params.portName = "COM_TEST";
    params.pollIntervalMs = 1;
    QString errorMessage;
    ASSERT_TRUE(session.open(params, &errorMessage));

    CaptureResult result;
    EXPECT_FALSE(session.capture(&result, &errorMessage));
    EXPECT_TRUE(errorMessage.contains("failure"));
}

TEST_F(ThreeDTempScannerTestBase, SessionPrefersIncompleteFrameMessageOverTransportTimeout) {
    FakeThermalTransport transport;
    transport.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
    QByteArray partialFrame = buildReadResponse(
        1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{kCaptureSuccessValue});
    partialFrame.chop(1);
    transport.enqueueRead(partialFrame);
    transport.readError = "Serial read timeout";

    ThermalSession session(&transport);
    ThermalTransportParams params;
    params.portName = "COM_TEST";
    params.pollIntervalMs = 1;
    params.timeoutMs = 10;
    params.scanTimeoutMs = 50;
    QString errorMessage;
    ASSERT_TRUE(session.open(params, &errorMessage));

    CaptureResult result;
    EXPECT_FALSE(session.capture(&result, &errorMessage));
    EXPECT_EQ(errorMessage, "Incomplete response frame");
}

TEST_F(ThreeDTempScannerTestBase, HandlerStatusReturnsReady) {
    ThreeDTempScannerHandler handler;
    JsonResponder responder;

    handler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastCode, 0);
    EXPECT_EQ(responder.lastData.value("status").toString(), "ready");
}

TEST_F(ThreeDTempScannerTestBase, HandlerValidatesSerialParams) {
    ThreeDTempScannerHandler handler;
    JsonResponder responder;

    handler.handle("capture", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("port_name"));

    handler.handle("capture", QJsonObject{{"port_name", "COM1"}, {"parity", "bad"}}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("parity"));
}

TEST_F(ThreeDTempScannerTestBase, HandlerUnknownCommandReturns404) {
    ThreeDTempScannerHandler handler;
    JsonResponder responder;

    handler.handle("not_a_real_cmd", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 404);
}

TEST_F(ThreeDTempScannerTestBase, HandlerFormatParameterOverridesOutputSuffix) {
    FakeThermalTransport fake;
    fake.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
    fake.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{kCaptureSuccessValue}));

    QVector<quint16> pixels;
    for (int i = 0; i < kImagePixelCount; ++i) {
        pixels.append(static_cast<quint16>(30000 + i));
    }
    fake.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), pixels));

    ThreeDTempScannerHandler handler;
    handler.setTransportFactory([&fake]() {
        return new NonOwningTransportWrapper(&fake);
    });

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString outputPath = dir.path() + "/thermal.csv";

    JsonResponder responder;
    handler.handle("capture", QJsonObject{
        {"port_name", "COM_TEST"},
        {"device_addr", 1},
        {"poll_interval_ms", 1},
        {"scan_timeout_ms", 50},
        {"output", outputPath},
        {"format", "json"}
    }, responder);

    ASSERT_TRUE(responder.lastStatus == "done")
        << " code=" << responder.lastCode
        << " message=" << responder.lastData.value("message").toString().toStdString();
    EXPECT_EQ(responder.lastData.value("format").toString(), "json");
    EXPECT_FALSE(responder.lastData.contains("board_temp_deg_c"));

    QFile file(outputPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    EXPECT_TRUE(content.trimmed().startsWith("{"));
}

TEST_F(ThreeDTempScannerTestBase, HandlerCaptureSavesAllFormats) {
    const struct {
        const char* format;
        const char* fileName;
    } cases[] = {
        {"png", "thermal.png"},
        {"json", "thermal.json"},
        {"csv", "thermal.csv"},
        {"raw", "thermal.raw"},
    };

    for (const auto& item : cases) {
        FakeThermalTransport fake;
        fake.enqueueRead(buildWriteResponse(1, kRegisterCaptureControl, kCaptureStartValue));
        fake.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), QVector<quint16>{kCaptureSuccessValue}));

        QVector<quint16> pixels;
        for (int i = 0; i < kImagePixelCount; ++i) {
            pixels.append(static_cast<quint16>(30000 + i));
        }
        fake.enqueueRead(buildReadResponse(1, static_cast<quint8>(FunctionCode::ReadHoldingRegisters), pixels));

        ThreeDTempScannerHandler handler;
        handler.setTransportFactory([&fake]() {
            return new NonOwningTransportWrapper(&fake);
        });

        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString outputPath = dir.path() + "/" + item.fileName;

        JsonResponder responder;
        handler.handle("capture", QJsonObject{
            {"port_name", "COM_TEST"},
            {"device_addr", 1},
            {"poll_interval_ms", 1},
            {"scan_timeout_ms", 50},
            {"output", outputPath},
            {"format", item.format}
        }, responder);

        ASSERT_TRUE(responder.lastStatus == "done")
            << item.format
            << " code=" << responder.lastCode
            << " message=" << responder.lastData.value("message").toString().toStdString();
        EXPECT_EQ(responder.lastData.value("output").toString(), outputPath);

        QFileInfo info(outputPath);
        ASSERT_TRUE(info.exists()) << item.format;
        ASSERT_GT(info.size(), 0) << item.format;

        if (QString::fromLatin1(item.format) == "png") {
            QImage image(outputPath);
            ASSERT_FALSE(image.isNull());
            EXPECT_EQ(image.width(), 320);
            EXPECT_EQ(image.height(), 240);
        }
    }
}

TEST_F(ThreeDTempScannerTestBase, MetadataContainsStatusAndCaptureOnly) {
    ThreeDTempScannerHandler handler;
    const auto& meta = handler.driverMeta();
    EXPECT_EQ(meta.info.id, "stdio.drv.3d_temp_scanner");
    EXPECT_EQ(meta.info.profiles, QStringList({"oneshot"}));

    ASSERT_NE(meta.findCommand("status"), nullptr);
    const auto* capture = meta.findCommand("capture");
    ASSERT_NE(capture, nullptr);
    EXPECT_EQ(meta.findCommand("test"), nullptr);
    EXPECT_EQ(meta.findCommand("get_board_temp"), nullptr);

    auto findParam = [&](const QString& name) -> const stdiolink::meta::FieldMeta* {
        for (const auto& param : capture->params) {
            if (param.name == name) {
                return &param;
            }
        }
        return nullptr;
    };

    ASSERT_NE(findParam("port_name"), nullptr);
    ASSERT_NE(findParam("format"), nullptr);
    ASSERT_NE(findParam("output"), nullptr);
    EXPECT_EQ(findParam("transport"), nullptr);
    EXPECT_EQ(findParam("host"), nullptr);
    EXPECT_EQ(findParam("port"), nullptr);
}

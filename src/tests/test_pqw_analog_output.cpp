#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include "driver_pqw_analog_output/handler.h"

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

const stdiolink::meta::FieldMeta* findParam(const stdiolink::meta::CommandMeta* command,
                                            const QString& name) {
    if (!command) {
        return nullptr;
    }
    for (const auto& param : command->params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

const stdiolink::meta::FieldMeta* findReturnField(const stdiolink::meta::CommandMeta* command,
                                                  const QString& name) {
    if (!command) {
        return nullptr;
    }
    for (const auto& field : command->returns.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

class PqwAnalogOutputHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
    }

    PqwAnalogOutputHandler handler;
    JsonResponder responder;
};

} // namespace

TEST(PqwAnalogOutputHelpersTest, EngineeringValueToRawRoundsToThreeDecimals) {
    quint16 rawValue = 0;
    QString errorMessage;
    ASSERT_TRUE(PqwAnalogOutputHandler::engineeringValueToRaw(12.345, rawValue, &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());
    EXPECT_EQ(rawValue, 12345);
}

TEST(PqwAnalogOutputHelpersTest, EngineeringValueToRawRejectsOutOfRange) {
    quint16 rawValue = 0;
    QString errorMessage;
    EXPECT_FALSE(PqwAnalogOutputHandler::engineeringValueToRaw(-0.001, rawValue, &errorMessage));
    EXPECT_TRUE(errorMessage.contains("0-65535"));
}

TEST(PqwAnalogOutputHelpersTest, CommWatchdogEncodingUses10msSteps) {
    quint16 rawValue = 0;
    QString errorMessage;
    ASSERT_TRUE(PqwAnalogOutputHandler::commWatchdogMsToRaw(120, rawValue, &errorMessage));
    EXPECT_EQ(rawValue, 13);
    EXPECT_EQ(PqwAnalogOutputHandler::commWatchdogRawToMs(rawValue), 120);
}

TEST(PqwAnalogOutputHelpersTest, CommWatchdogEncodingRejectsNon10msStep) {
    quint16 rawValue = 0;
    QString errorMessage;
    EXPECT_FALSE(PqwAnalogOutputHandler::commWatchdogMsToRaw(125, rawValue, &errorMessage));
    EXPECT_TRUE(errorMessage.contains("multiple of 10"));
}

TEST(PqwAnalogOutputHelpersTest, ResolveConnectionOptionsForSetCommConfigUsesCurrentFields) {
    PqwAnalogOutputConnectionOptions options;
    QString errorMessage;
    ASSERT_TRUE(PqwAnalogOutputHandler::resolveConnectionOptions(
        "set_comm_config",
        QJsonObject{
            {"port_name", "COM7"},
            {"current_unit_id", 1},
            {"current_baud_rate", 9600},
            {"current_parity", "none"},
            {"current_stop_bits", "1"},
            {"unit_id", 2},
            {"baud_rate", 19200},
            {"parity", "even"},
            {"stop_bits", "2"}
        },
        options,
        &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());
    EXPECT_EQ(options.portName, "COM7");
    EXPECT_EQ(options.unitId, 1);
    EXPECT_EQ(options.baudRate, 9600);
    EXPECT_EQ(options.parity, "none");
    EXPECT_EQ(options.stopBits, "1");
}

TEST(PqwAnalogOutputHelpersTest, BuildCommConfigWritesUsesTargetFields) {
    QVector<PqwAnalogOutputConfigWrite> writes;
    QString errorMessage;
    ASSERT_TRUE(PqwAnalogOutputHandler::buildCommConfigWrites(
        QJsonObject{
            {"unit_id", 2},
            {"baud_rate", 19200},
            {"parity", "even"},
            {"stop_bits", "2"},
            {"comm_watchdog_ms", 120}
        },
        writes,
        &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());
    ASSERT_EQ(writes.size(), 5);
    EXPECT_EQ(writes[0].field, "unit_id");
    EXPECT_EQ(writes[0].rawValue, 2);
    EXPECT_EQ(writes[1].field, "baud_rate");
    EXPECT_EQ(writes[1].rawValue, 3);
    EXPECT_EQ(writes[2].field, "parity");
    EXPECT_EQ(writes[2].rawValue, 2);
    EXPECT_EQ(writes[3].field, "stop_bits");
    EXPECT_EQ(writes[3].rawValue, 1);
    EXPECT_EQ(writes[4].field, "comm_watchdog_ms");
    EXPECT_EQ(writes[4].rawValue, 13);
}

TEST_F(PqwAnalogOutputHandlerTest, StatusReturnsReady) {
    handler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastCode, 0);
    EXPECT_EQ(responder.lastData.value("status").toString(), "ready");
}

TEST_F(PqwAnalogOutputHandlerTest, DeviceCommandsRequirePortName) {
    handler.handle("get_config", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("port_name"));
}

TEST_F(PqwAnalogOutputHandlerTest, UnitIdMustBeWithin255Range) {
    handler.handle("get_config", QJsonObject{
        {"port_name", "COM_TEST"},
        {"unit_id", 256}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("1-255"));
}

TEST_F(PqwAnalogOutputHandlerTest, ReadOutputsRejectsOutOfRangeCount) {
    handler.handle("read_outputs", QJsonObject{
        {"port_name", "COM_TEST"},
        {"start_channel", 10},
        {"count", 10}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("18 channels"));
}

TEST_F(PqwAnalogOutputHandlerTest, WriteOutputRejectsInvalidChannelBeforePortOpen) {
    handler.handle("write_output", QJsonObject{
        {"port_name", "COM_TEST"},
        {"channel", 19},
        {"value", 1.234}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("channel"));
}

TEST_F(PqwAnalogOutputHandlerTest, SetCommConfigRejectsInvalidWatchdogValue) {
    handler.handle("set_comm_config", QJsonObject{
        {"port_name", "COM_TEST"},
        {"comm_watchdog_ms", 125}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("10 ms"));
}

TEST_F(PqwAnalogOutputHandlerTest, MetadataContainsExpectedCommandsAndDefaults) {
    const auto& meta = handler.driverMeta();
    EXPECT_EQ(meta.info.id, "stdio.drv.pqw_analog_output");
    EXPECT_EQ(meta.info.name, QString::fromUtf8("品全微模拟量模块"));

    const auto* getConfig = meta.findCommand("get_config");
    const auto* readOutputs = meta.findCommand("read_outputs");
    const auto* writeOutputs = meta.findCommand("write_outputs");
    const auto* setCommConfig = meta.findCommand("set_comm_config");
    ASSERT_NE(getConfig, nullptr);
    ASSERT_NE(readOutputs, nullptr);
    ASSERT_NE(writeOutputs, nullptr);
    ASSERT_NE(setCommConfig, nullptr);

    const auto* portName = findParam(getConfig, "port_name");
    const auto* baudRate = findParam(getConfig, "baud_rate");
    const auto* parity = findParam(getConfig, "parity");
    const auto* stopBits = findParam(getConfig, "stop_bits");
    const auto* unitId = findParam(getConfig, "unit_id");
    ASSERT_NE(portName, nullptr);
    ASSERT_NE(baudRate, nullptr);
    ASSERT_NE(parity, nullptr);
    ASSERT_NE(stopBits, nullptr);
    ASSERT_NE(unitId, nullptr);
    EXPECT_TRUE(portName->required);
    EXPECT_EQ(baudRate->defaultValue.toInt(), 9600);
    EXPECT_EQ(parity->defaultValue.toString(), "none");
    EXPECT_EQ(stopBits->defaultValue.toString(), "1");
    EXPECT_EQ(unitId->defaultValue.toInt(), 1);

    const auto* count = findParam(readOutputs, "count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->defaultValue.toInt(), 18);

    const auto* values = findParam(writeOutputs, "values");
    ASSERT_NE(values, nullptr);
    EXPECT_TRUE(values->required);
    EXPECT_EQ(values->constraints.minItems.value_or(0), 1);

    const auto* writesField = findReturnField(setCommConfig, "writes");
    const auto* rebootField = findReturnField(setCommConfig, "reboot_required");
    const auto* currentUnitId = findParam(setCommConfig, "current_unit_id");
    const auto* currentBaudRate = findParam(setCommConfig, "current_baud_rate");
    const auto* currentParity = findParam(setCommConfig, "current_parity");
    const auto* currentStopBits = findParam(setCommConfig, "current_stop_bits");
    ASSERT_NE(writesField, nullptr);
    ASSERT_NE(rebootField, nullptr);
    ASSERT_NE(currentUnitId, nullptr);
    ASSERT_NE(currentBaudRate, nullptr);
    ASSERT_NE(currentParity, nullptr);
    ASSERT_NE(currentStopBits, nullptr);
    EXPECT_EQ(writesField->type, stdiolink::meta::FieldType::Array);
    EXPECT_EQ(rebootField->type, stdiolink::meta::FieldType::Bool);
    EXPECT_EQ(currentUnitId->defaultValue.toInt(), 1);
    EXPECT_EQ(currentBaudRate->defaultValue.toInt(), 9600);
    EXPECT_EQ(currentParity->defaultValue.toString(), "none");
    EXPECT_EQ(currentStopBits->defaultValue.toString(), "1");
}

TEST_F(PqwAnalogOutputHandlerTest, AllCommandsHaveGeneratedExamples) {
    const auto& meta = handler.driverMeta();
    for (const auto& command : meta.commands) {
        bool hasStdio = false;
        bool hasConsole = false;
        for (const auto& example : command.examples) {
            const QString mode = example.value("mode").toString();
            if (mode == "stdio") {
                hasStdio = true;
            }
            if (mode == "console") {
                hasConsole = true;
            }
        }
        EXPECT_TRUE(hasStdio) << command.name.toStdString();
        EXPECT_TRUE(hasConsole) << command.name.toStdString();
    }
}

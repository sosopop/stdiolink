#include <QCoreApplication>
#include <QJsonObject>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class ConsoleMetaStubHandler : public IMetaCommandHandler {
public:
    ConsoleMetaStubHandler() {
        FieldBuilder unitItem("unit", FieldType::Object);
        unitItem.addField(FieldBuilder("id", FieldType::Int).required())
            .addField(FieldBuilder("size", FieldType::Int));

        FieldBuilder roiField("roi", FieldType::Object);
        roiField.addField(FieldBuilder("x", FieldType::Int))
            .addField(FieldBuilder("y", FieldType::Int));

        m_meta = DriverMetaBuilder()
                     .schemaVersion("1.0.0")
                     .info("test-console-meta-driver",
                           "Test Console Meta Driver",
                           "1.0.0",
                           "Console meta-aware parsing test driver")
                     .entry("test_console_meta_driver")
                     .command(CommandBuilder("run")
                                  .description("Echo validated params")
                                  .param(FieldBuilder("password", FieldType::String).required())
                                  .param(FieldBuilder("mode_code", FieldType::Enum)
                                             .required()
                                             .enumValues(QStringList{"1", "2"}))
                                  .param(FieldBuilder("safe_counter", FieldType::Int64))
                                  .param(roiField)
                                  .param(FieldBuilder("units", FieldType::Array)
                                             .items(unitItem))
                                  .example("Console echo example",
                                           "console",
                                           QJsonObject{{"password", "123456"},
                                                       {"mode_code", "1"},
                                                       {"units", QJsonArray{QJsonObject{{"id", 1}, {"size", 10000}}}}})
                                  .example("Stdio echo example",
                                           "stdio",
                                           QJsonObject{{"password", "123456"},
                                                       {"mode_code", "1"},
                                                       {"units", QJsonArray{QJsonObject{{"id", 1}, {"size", 10000}}}}})
                                  .returns(FieldType::Object, "Echo payload"))
                     .build();
    }

    const DriverMeta& driverMeta() const override {
        return m_meta;
    }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& responder) override {
        if (cmd == "run") {
            responder.done(0, data);
            return;
        }
        responder.error(404, QJsonObject{{"message", "unknown command"}});
    }

private:
    DriverMeta m_meta;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    ConsoleMetaStubHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}

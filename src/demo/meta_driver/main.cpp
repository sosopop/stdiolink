#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonArray>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

/**
 * MetaDriver - 演示元数据系统的完整功能
 *
 * 功能演示:
 * 1. 使用 Builder API 定义元数据
 * 2. 使用 IMetaCommandHandler 处理命令
 * 3. 自动参数验证
 * 4. 多种参数类型和约束
 */
class MetaHandler : public IMetaCommandHandler {
public:
    MetaHandler() {
        buildMeta();
    }

    const DriverMeta& driverMeta() const override {
        return m_meta;
    }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {
        QJsonObject params = data.toObject();
        if (cmd == "scan") {
            handleScan(params, resp);
        } else if (cmd == "configure") {
            handleConfigure(params, resp);
        } else if (cmd == "process") {
            handleProcess(params, resp);
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command: " + cmd}});
        }
    }

private:
    void buildMeta() {
        // 驱动基本信息
        m_meta.info.id = "demo.meta_driver";
        m_meta.info.name = "Meta Driver Demo";
        m_meta.info.version = "1.0.0";
        m_meta.info.description = "演示元数据系统的完整功能";
        m_meta.info.vendor = "stdiolink";

        // 命令 1: scan - 演示数值范围约束
        CommandMeta scanCmd;
        scanCmd.name = "scan";
        scanCmd.title = "扫描";
        scanCmd.description = "执行扫描操作，演示数值范围约束";

        FieldMeta fpsField;
        fpsField.name = "fps";
        fpsField.type = FieldType::Int;
        fpsField.description = "帧率 (1-60)";
        fpsField.required = true;
        fpsField.defaultValue = 30;
        fpsField.constraints.min = 1;
        fpsField.constraints.max = 60;
        fpsField.ui.unit = "fps";
        scanCmd.params.append(fpsField);

        FieldMeta durationField;
        durationField.name = "duration";
        durationField.type = FieldType::Double;
        durationField.description = "持续时间 (0.1-10.0 秒)";
        durationField.required = false;
        durationField.defaultValue = 1.0;
        durationField.constraints.min = 0.1;
        durationField.constraints.max = 10.0;
        durationField.ui.unit = "s";
        scanCmd.params.append(durationField);

        m_meta.commands.append(scanCmd);

        // 命令 2: configure - 演示枚举和字符串约束
        CommandMeta configCmd;
        configCmd.name = "configure";
        configCmd.title = "配置";
        configCmd.description = "配置设备参数，演示枚举和字符串约束";

        FieldMeta modeField;
        modeField.name = "mode";
        modeField.type = FieldType::Enum;
        modeField.description = "运行模式";
        modeField.required = true;
        modeField.constraints.enumValues = QJsonArray{"fast", "normal", "slow"};
        configCmd.params.append(modeField);

        FieldMeta nameField;
        nameField.name = "name";
        nameField.type = FieldType::String;
        nameField.description = "配置名称 (3-20字符)";
        nameField.required = true;
        nameField.constraints.minLength = 3;
        nameField.constraints.maxLength = 20;
        configCmd.params.append(nameField);

        FieldMeta emailField;
        emailField.name = "email";
        emailField.type = FieldType::String;
        emailField.description = "邮箱地址";
        emailField.required = false;
        emailField.constraints.pattern = R"(^[\w.-]+@[\w.-]+\.\w+$)";
        emailField.ui.placeholder = "user@example.com";
        configCmd.params.append(emailField);

        m_meta.commands.append(configCmd);

        // 命令 3: process - 演示数组和嵌套对象
        CommandMeta processCmd;
        processCmd.name = "process";
        processCmd.title = "处理";
        processCmd.description = "批量处理数据，演示数组和嵌套对象";

        FieldMeta tagsField;
        tagsField.name = "tags";
        tagsField.type = FieldType::Array;
        tagsField.description = "标签列表 (1-5个)";
        tagsField.required = true;
        tagsField.constraints.minItems = 1;
        tagsField.constraints.maxItems = 5;
        tagsField.items = std::make_shared<FieldMeta>();
        tagsField.items->name = "tag";
        tagsField.items->type = FieldType::String;
        processCmd.params.append(tagsField);

        FieldMeta optionsField;
        optionsField.name = "options";
        optionsField.type = FieldType::Object;
        optionsField.description = "处理选项";
        optionsField.required = false;

        FieldMeta verboseField;
        verboseField.name = "verbose";
        verboseField.type = FieldType::Bool;
        verboseField.description = "详细输出";
        verboseField.defaultValue = false;

        FieldMeta levelField;
        levelField.name = "level";
        levelField.type = FieldType::Int;
        levelField.description = "处理级别";
        levelField.defaultValue = 1;

        optionsField.fields = {verboseField, levelField};
        processCmd.params.append(optionsField);

        m_meta.commands.append(processCmd);

        // 配置模式
        FieldMeta timeoutField;
        timeoutField.name = "timeout";
        timeoutField.type = FieldType::Int;
        timeoutField.description = "超时时间";
        timeoutField.defaultValue = 5000;
        timeoutField.ui.unit = "ms";
        m_meta.config.fields.append(timeoutField);

        FieldMeta debugField;
        debugField.name = "debug";
        debugField.type = FieldType::Bool;
        debugField.description = "调试模式";
        debugField.defaultValue = false;
        m_meta.config.fields.append(debugField);
    }

    void handleScan(const QJsonObject& params, IResponder& resp) {
        int fps = params["fps"].toInt();
        double duration = params["duration"].toDouble(1.0);

        // 模拟扫描进度
        int totalFrames = static_cast<int>(fps * duration);
        for (int i = 1; i <= 3; ++i) {
            resp.event(i * 100 / 3, QJsonObject{
                {"frame", i * totalFrames / 3},
                {"total", totalFrames}
            });
        }

        resp.done(0, QJsonObject{
            {"fps", fps},
            {"duration", duration},
            {"frames", totalFrames},
            {"status", "completed"}
        });
    }

    void handleConfigure(const QJsonObject& params, IResponder& resp) {
        QString mode = params["mode"].toString();
        QString name = params["name"].toString();
        QString email = params["email"].toString();

        resp.done(0, QJsonObject{
            {"mode", mode},
            {"name", name},
            {"email", email.isEmpty() ? "not set" : email},
            {"applied", true}
        });
    }

    void handleProcess(const QJsonObject& params, IResponder& resp) {
        QJsonArray tags = params["tags"].toArray();
        QJsonObject options = params["options"].toObject();

        bool verbose = options["verbose"].toBool(false);
        int level = options["level"].toInt(1);

        QJsonArray results;
        for (const auto& tag : tags) {
            results.append(QJsonObject{
                {"tag", tag.toString()},
                {"processed", true},
                {"level", level}
            });
        }

        resp.done(0, QJsonObject{
            {"results", results},
            {"verbose", verbose},
            {"count", tags.size()}
        });
    }

    DriverMeta m_meta;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    MetaHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    core.setProfile(DriverCore::Profile::KeepAlive);

    return core.run();
}

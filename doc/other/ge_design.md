StdioLink v2.0 自描述驱动框架 - 需求规格说明书与详细设计1. 概述 (Overview)1.1 背景目前的 stdiolink 提供了一个基于 JSONL stdio 的基础通信通道。Driver（驱动）与 Host（宿主）之间的交互依赖于隐式的约定（文档或口头协议）。当 Driver 增加新命令或修改参数时，Host 端通常需要修改代码甚至 UI 才能适配，导致维护成本高，且容易出现接口不匹配的问题。1.2 目标引入“自描述（Self-Description）”机制，使 Driver 能够通过标准协议向 Host 暴露其能力（Capabilities）、配置模式（Configuration Schema）和命令接口（Command Interface）。实现“一次定义，自动生成”的效果：Driver 自文档化：代码即文档，接口定义与逻辑绑定。Host 自动 UI：Host 可根据元数据自动生成配置表单和命令调用界面。自动校验：框架层自动处理参数类型检查和必填校验，业务逻辑无需处理非法输入。2. 核心概念 (Core Concepts)Manifest (清单): Driver 的静态描述信息，包含名称、版本、作者、图标等。Schema (模式): 用于描述数据结构的元数据，类似于 JSON Schema，但更轻量，用于描述配置项和命令参数。Registry (注册表): Driver 内部用于注册命令、定义参数模板并绑定处理函数的组件。Introspection (内省): Host 在运行时查询 Driver 元数据的过程。3. 功能需求 (Functional Requirements)3.1 协议扩展系统命令保留: 定义 sys.* 命名空间，所有 Driver 必须内置支持 sys.get_meta 命令，用于返回完整的元数据描述。标准错误码: 定义参数校验失败的标准错误码（如 400 Bad Request）。3.2 元数据定义 (Metadata Definition)Driver 必须能够定义以下元数据：全局配置 (Config): Driver 运行所需的初始化参数（如：API Key、数据库路径、端口号）。命令列表 (Commands):命令名称 (Command Name)功能描述 (Description)输入参数 (Arguments): 字段名、类型、默认值、是否必填、验证规则（Min/Max/Regex）、UI 提示（Label/Tooltip）。输出结构 (Returns): 预期返回的数据结构描述（用于 Host 生成结果视图）。3.3 类型系统 (Type System)支持以下基础类型，以便 UI 渲染：String (单行文本), Text (多行文本), Password (掩码)Integer (整型, 支持 Range), Double (浮点型, 支持 Precision)Boolean (开关/复选框)Enum (下拉列表/单选组)File/Directory (文件路径选择器)Object (嵌套结构)3.4 注册与绑定 (Registry & Binding)提供 C++ 流式 API（Fluent API），在编译期构建元数据，并将其直接绑定到 C++ Lambda 或函数指针，避免元数据与实现脱节。4. 详细设计 (Detailed Design)4.1 通信协议升级4.1.1 元数据查询命令Host 发送：{ "cmd": "sys.get_meta" }
Driver 响应 (Payload 示例):{
  "info": {
    "name": "ImageProcessor",
    "version": "1.0.2",
    "description": "High performance image manipulation driver"
  },
  "config": { // 全局配置模式
    "fields": [
      { "name": "cache_dir", "type": "directory", "label": "Cache Path", "required": true }
    ]
  },
  "commands": [
    {
      "name": "resize",
      "description": "Resize an image",
      "args": [
        { "name": "path", "type": "file", "label": "Input Image", "required": true },
        { "name": "width", "type": "int", "label": "Target Width", "min": 1, "max": 4096, "default": 800 }
      ]
    }
  ]
}
4.2 C++ 核心类设计 (stdiolink 库扩展)我们将引入一个新的命名空间 stdiolink::meta 和核心类 Registry。4.2.1 元数据结构体namespace stdiolink {

enum class FieldType {
    String, Text, Password, Integer, Double, Boolean, Enum, File, Directory, Object
};

struct FieldMeta {
    QString name;
    FieldType type;
    QString label;
    QString description;
    QJsonValue defaultValue;
    bool required = true;
    
    // 约束
    QVariant min;
    QVariant max;
    QStringList enumOptions; // 用于 Enum 类型
};

struct CommandMeta {
    QString name;
    QString description;
    QVector<FieldMeta> args;
    // 输出 schema 暂时留空，后续版本支持
};

struct DriverMeta {
    QString name;
    QString version;
    QString description;
    QVector<FieldMeta> globalConfig;
    QVector<CommandMeta> commands;
};

}
4.2.2 注册表 (Registry) - 核心驱动引擎Registry 类将替代原有的 ICommandHandler 的手动实现方式。它既存储元数据，也存储函数回调。#include <functional>
#include <QMap>

namespace stdiolink {

// 命令处理函数的签名：入参是经过校验的 JSON 对象，Responder 用于回传
using TypedHandler = std::function<void(const QJsonObject& args, IResponder& responder)>;

class Registry : public ICommandHandler {
public:
    Registry() {
        // 自动注册系统命令
        registerSystemCommands();
    }

    // 设置 Driver 基本信息
    Registry& info(const QString& name, const QString& version, const QString& desc);

    // 定义全局配置参数
    Registry& config(const QString& name, FieldType type, const QString& label);

    // 开始定义一个命令 (Builder 模式)
    class CommandBuilder {
    public:
        CommandBuilder& desc(const QString& text);
        
        // 添加参数定义
        CommandBuilder& arg(const QString& name, FieldType type, const QString& label, 
                            const QJsonValue& def = {}, bool required = true);
        
        // 增加约束 (例如 Integer 的范围)
        CommandBuilder& range(int min, int max);
        CommandBuilder& options(const QStringList& opts);

        // 最终绑定实现函数
        void handler(TypedHandler h);

    private:
        Registry* m_reg;
        CommandMeta m_meta;
        // ...
    };

    CommandBuilder command(const QString& cmdName);

    // 实现 ICommandHandler 接口
    void handle(const QString& cmd, const QJsonValue& data, IResponder& responder) override;

private:
    DriverMeta m_metaData;
    QMap<QString, TypedHandler> m_handlers;

    void registerSystemCommands();
    // 校验输入数据是否符合 Schema
    bool validateArgs(const QVector<FieldMeta>& schema, const QJsonObject& input, QString& err);
};

}
4.3 使用示例 (Developer Experience)这是开发一个新 Driver 时的代码样子。开发者不再需要手动解析 JSON，只需定义接口并编写业务逻辑。int main(int argc, char* argv[]) {
    stdiolink::DriverCore core;
    stdiolink::Registry registry;

    // 1. 定义 Driver 信息
    registry.info("VideoConverter", "1.0.0", "FFmpeg based video tool");

    // 2. 注册命令：resize
    registry.command("resize")
        .desc("Resize video resolution")
        // 定义参数：输入文件
        .arg("input", stdiolink::FieldType::File, "Input File")
        // 定义参数：宽度 (带默认值和范围)
        .arg("width", stdiolink::FieldType::Integer, "Width", 1920)
            .range(100, 7680)
        // 定义参数：格式 (枚举)
        .arg("format", stdiolink::FieldType::Enum, "Output Format", "mp4")
            .options({"mp4", "mkv", "avi"})
        // 3. 绑定实现逻辑
        .handler([](const QJsonObject& args, stdiolink::IResponder& resp) {
            // 此时 args 已经经过框架校验，字段存在且类型正确
            QString input = args["input"].toString();
            int width = args["width"].toInt();
            QString fmt = args["format"].toString();

            resp.event(0, "Converting...");
            // ... 业务逻辑 ...
            resp.done(0, "Success");
        });

    // 4. 启动 Driver
    core.setHandler(&registry);
    return core.run();
}
4.4 内部逻辑流程启动: Driver 启动，构建内存中的 DriverMeta 结构。Host 握手: Host 启动 Driver 进程后，立即发送 sys.get_meta。内省: Registry 的内置 Handler 捕获 sys.get_meta，将 m_metaData 序列化为 JSON 返回给 Host。调用:Host 发送 {"cmd": "resize", "data": {"width": "bad"}}。Registry::handle 拦截请求。查找 "resize" 的元数据 CommandMeta。自动校验: 发现 "width" 应为 Int 且 input 为必填。校验失败: 直接调用 resp.error(400, "Argument validation failed: 'width' must be integer")。校验成功: 调用开发者绑定的 Lambda 函数。5. Host 端 UI 自动生成策略虽然 stdiolink 是 C++ 库，但 Host 端通常有 GUI。设计建议如下：5.1 UI Generator (Host 侧)Host 应实现一个 FormBuilder 类：获取 Meta: 解析 sys.get_meta 的 JSON 响应。渲染 Config: 根据 config 字段生成“设置页面”。渲染 Commands:创建一个下拉框或列表显示所有可用命令。当选中某个命令时，动态生成右侧/下方的参数表单。控件映射:FieldType::File -> QLineEdit + QToolButton (FileDialog)FieldType::Enum -> QComboBoxFieldType::Boolean -> QCheckBoxFieldType::Integer -> QSpinBox (设置 min/max)5.2 调用代理UI 上的“执行”按钮不直接写死 JSON，而是：遍历生成的 Form 控件。收集所有值，组装成 QJsonObject。调用 driver.request(selectedCommand, collectedData)。6. 开发计划 (Roadmap)阶段 1: 基础元数据架构 (v2.0-alpha)[ ] 定义 FieldMeta, CommandMeta 结构体。[ ] 实现 Registry 类及其 Builder 模式 API。[ ] 实现 sys.get_meta 的自动响应。[ ] 替换 DriverCore 中的 ICommandHandler 为 Registry。阶段 2: 校验层 (v2.0-beta)[ ] 实现 Validator 类，支持类型检查、必填检查。[ ] 支持 Min, Max, Regex 约束检查。[ ] 在 Registry::handle 中集成校验逻辑。阶段 3: Host 辅助工具 (v2.1)[ ] 在 Host 端增加 MetaClient 封装，方便获取和缓存 Meta。[ ] 提供 schema.json 导出功能（方便前端/Web 界面使用）。阶段 4: UI 生成 Demo (Optional)[ ] 编写一个 Qt Widget 示例，展示如何读取 Driver 并生成界面。7. 优势总结解耦: Driver 开发者只关心 C++ 逻辑，Host 开发者只关心通用 UI 渲染。健壮: 所有的参数校验下沉到框架层，减少了业务代码中的 if (data.contains("key")) 样板代码。可扩展: 未来可以轻松增加新的参数类型（如 Color, Date），只需更新协议和 UI 映射，无需修改具体 Driver 逻辑。
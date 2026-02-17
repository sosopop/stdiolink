#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include "stdiolink/doc/doc_generator.h"
#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink;

// ============================================
// 测试辅助函数
// ============================================

meta::DriverMeta createTestMeta() {
    meta::DriverMeta meta;
    meta.info.name = "TestDriver";
    meta.info.version = "1.0.0";
    meta.info.description = "A test driver";
    meta.info.vendor = "TestVendor";

    // 添加命令
    meta::CommandMeta cmd;
    cmd.name = "scan";
    cmd.title = "Scan Device";
    cmd.description = "Scan for devices";

    meta::FieldMeta param;
    param.name = "timeout";
    param.type = meta::FieldType::Int;
    param.required = true;
    param.description = "Timeout in ms";
    param.constraints.min = 100;
    param.constraints.max = 10000;
    cmd.params.append(param);

    meta::FieldMeta retField;
    retField.name = "devices";
    retField.type = meta::FieldType::Array;
    retField.description = "List of devices";
    cmd.returns.fields.append(retField);

    meta.commands.append(cmd);

    // 添加配置
    meta::FieldMeta configField;
    configField.name = "port";
    configField.type = meta::FieldType::String;
    configField.description = "Serial port";
    configField.defaultValue = "COM1";
    meta.config.fields.append(configField);

    return meta;
}

// ============================================
// Markdown 生成测试
// ============================================

TEST(DocGenerator, MarkdownTitle) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("# TestDriver"));
}

TEST(DocGenerator, MarkdownVersion) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("**Version:** 1.0.0"));
}

TEST(DocGenerator, MarkdownCommands) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("## Commands"));
    EXPECT_TRUE(md.contains("### scan"));
}

TEST(DocGenerator, MarkdownParameters) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("#### Parameters"));
    EXPECT_TRUE(md.contains("| timeout |"));
}

TEST(DocGenerator, MarkdownConstraints) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("Range: 100-10000"));
}

TEST(DocGenerator, MarkdownConfig) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("## Configuration"));
    EXPECT_TRUE(md.contains("| port |"));
}

// ============================================
// OpenAPI 生成测试
// ============================================

TEST(DocGenerator, OpenAPIVersion) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    EXPECT_EQ(api["openapi"].toString(), "3.0.3");
}

TEST(DocGenerator, OpenAPIInfo) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto info = api["info"].toObject();
    EXPECT_EQ(info["title"].toString(), "TestDriver");
    EXPECT_EQ(info["version"].toString(), "1.0.0");
}

TEST(DocGenerator, OpenAPIPaths) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto paths = api["paths"].toObject();
    EXPECT_TRUE(paths.contains("/scan"));
}

TEST(DocGenerator, OpenAPIRequestBody) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto paths = api["paths"].toObject();
    auto scan = paths["/scan"].toObject();
    auto post = scan["post"].toObject();
    EXPECT_TRUE(post.contains("requestBody"));
}

TEST(DocGenerator, OpenAPISchema) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto paths = api["paths"].toObject();
    auto scan = paths["/scan"].toObject();
    auto post = scan["post"].toObject();
    auto reqBody = post["requestBody"].toObject();
    auto content = reqBody["content"].toObject();
    auto json = content["application/json"].toObject();
    auto schema = json["schema"].toObject();
    auto props = schema["properties"].toObject();
    EXPECT_TRUE(props.contains("timeout"));
}

// ============================================
// HTML 生成测试
// ============================================

TEST(DocGenerator, HtmlDoctype) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.startsWith("<!DOCTYPE html>"));
}

TEST(DocGenerator, HtmlTitle) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.contains("<title>TestDriver Documentation</title>"));
}

TEST(DocGenerator, HtmlStyle) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.contains("<style>"));
    EXPECT_TRUE(html.contains("</style>"));
}

TEST(DocGenerator, HtmlCommands) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.contains("<h2>Commands</h2>"));
    EXPECT_TRUE(html.contains("<h3>scan</h3>"));
}

TEST(DocGenerator, HtmlTable) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.contains("<table>"));
    EXPECT_TRUE(html.contains("<th>Name</th>"));
}

// ============================================
// 边界情况测试
// ============================================

TEST(DocGenerator, EmptyMeta) {
    meta::DriverMeta meta;
    meta.info.name = "Empty";

    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("# Empty"));
    EXPECT_FALSE(md.contains("## Commands"));

    QJsonObject api = DocGenerator::toOpenAPI(meta);
    EXPECT_TRUE(api["paths"].toObject().isEmpty());

    QString html = DocGenerator::toHtml(meta);
    EXPECT_TRUE(html.contains("<h1 class=\"page-title\">Empty</h1>"));
}

TEST(DocGenerator, CommandPathConversion) {
    meta::DriverMeta meta;
    meta.info.name = "Test";

    meta::CommandMeta cmd;
    cmd.name = "mesh.union";
    meta.commands.append(cmd);

    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto paths = api["paths"].toObject();
    EXPECT_TRUE(paths.contains("/mesh/union"));
}

TEST(DocGenerator, EnumConstraints) {
    meta::DriverMeta meta;
    meta.info.name = "Test";

    meta::CommandMeta cmd;
    cmd.name = "setMode";

    meta::FieldMeta param;
    param.name = "mode";
    param.type = meta::FieldType::Enum;
    param.constraints.enumValues = QJsonArray{"fast", "slow", "auto"};
    cmd.params.append(param);

    meta.commands.append(cmd);

    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("`fast`"));
    EXPECT_TRUE(md.contains("`slow`"));

    QJsonObject api = DocGenerator::toOpenAPI(meta);
    auto paths = api["paths"].toObject();
    auto setMode = paths["/setMode"].toObject();
    auto post = setMode["post"].toObject();
    auto reqBody = post["requestBody"].toObject();
    auto content = reqBody["content"].toObject();
    auto json = content["application/json"].toObject();
    auto schema = json["schema"].toObject();
    auto props = schema["properties"].toObject();
    auto modeSchema = props["mode"].toObject();
    EXPECT_TRUE(modeSchema.contains("enum"));
}

// ============================================
// TypeScript 生成测试 (M26)
// ============================================

TEST(DocGenerator, TypeScriptHeaderAndBaseTypes) {
    auto meta = createTestMeta();
    QString ts = DocGenerator::toTypeScript(meta);

    EXPECT_TRUE(ts.contains("@version 1.0.0"));
    EXPECT_TRUE(ts.contains("@vendor TestVendor"));
    EXPECT_TRUE(ts.contains("export interface TaskMessage"));
    EXPECT_TRUE(ts.contains("export interface Driver"));
}

TEST(DocGenerator, TypeScriptCommandInterfaces) {
    auto meta = createTestMeta();
    QString ts = DocGenerator::toTypeScript(meta);

    EXPECT_TRUE(ts.contains("export interface ScanParams"));
    EXPECT_TRUE(ts.contains("timeout: number;"));
    EXPECT_TRUE(ts.contains("export interface ScanResult"));
    EXPECT_TRUE(ts.contains("devices?: any[];"));
}

TEST(DocGenerator, TypeScriptComplexTypeMapping) {
    meta::DriverMeta meta;
    meta.info.name = "TsTest";
    meta.info.version = "1.0.0";

    meta::CommandMeta cmd;
    cmd.name = "setMode";

    meta::FieldMeta mode;
    mode.name = "mode";
    mode.type = meta::FieldType::Enum;
    mode.required = true;
    mode.constraints.enumValues = QJsonArray{"fast", "slow"};
    cmd.params.append(mode);

    meta::FieldMeta fps;
    fps.name = "fps";
    fps.type = meta::FieldType::Int;
    fps.defaultValue = 30;
    cmd.params.append(fps);

    meta::FieldMeta names;
    names.name = "names";
    names.type = meta::FieldType::Array;
    names.items = std::make_shared<meta::FieldMeta>();
    names.items->type = meta::FieldType::String;
    cmd.params.append(names);

    meta::FieldMeta options;
    options.name = "options";
    options.type = meta::FieldType::Object;
    meta::FieldMeta enabled;
    enabled.name = "enabled";
    enabled.type = meta::FieldType::Bool;
    enabled.required = true;
    options.fields.append(enabled);
    cmd.params.append(options);

    meta::FieldMeta result;
    result.name = "ok";
    result.type = meta::FieldType::Bool;
    cmd.returns.fields.append(result);

    meta.commands.append(cmd);

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_TRUE(ts.contains("mode: 'fast' | 'slow';"));
    EXPECT_TRUE(ts.contains("fps?: number;"));
    EXPECT_TRUE(ts.contains("@default 30"));
    EXPECT_TRUE(ts.contains("names?: string[];"));
    EXPECT_TRUE(ts.contains("options?: {"));
    EXPECT_TRUE(ts.contains("enabled: boolean;"));
    EXPECT_TRUE(ts.contains("ok?: boolean;"));
}

TEST(DocGenerator, TypeScriptProxyInterface) {
    meta::DriverMeta meta;
    meta.info.name = "Proxy Driver";

    meta::CommandMeta cmd1;
    cmd1.name = "scan";
    meta.commands.append(cmd1);

    meta::CommandMeta cmd2;
    cmd2.name = "mesh.union";
    meta.commands.append(cmd2);

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_TRUE(ts.contains("export interface ProxyDriverProxy"));
    EXPECT_TRUE(ts.contains("scan(params?: ScanParams): Promise<ScanResult>;"));
    EXPECT_TRUE(ts.contains("'mesh.union'(params?: MeshUnionParams): Promise<MeshUnionResult>;"));
    EXPECT_TRUE(ts.contains("readonly $driver: Driver;"));
    EXPECT_TRUE(ts.contains("readonly $meta: object;"));
    EXPECT_TRUE(ts.contains("$rawRequest(cmd: string, data?: any): Task;"));
    EXPECT_TRUE(ts.contains("$close(): void;"));
    EXPECT_TRUE(ts.contains("export type DriverProxy = ProxyDriverProxy;"));
}

TEST(DocGenerator, TypeScriptEmptyMeta) {
    meta::DriverMeta meta;
    meta.info.name = "Empty";

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_FALSE(ts.isEmpty());
    EXPECT_TRUE(ts.contains("export interface EmptyProxy"));
}

TEST(DocGenerator, TypeScriptExportDocCli) {
    QString exe = PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "stdio.drv.calculator");
    ASSERT_TRUE(QFileInfo::exists(exe));

    QProcess proc;
    proc.start(exe, {"--export-doc=ts"});
    ASSERT_TRUE(proc.waitForFinished(10000));
    EXPECT_EQ(proc.exitCode(), 0);

    QString out = QString::fromUtf8(proc.readAllStandardOutput());
    EXPECT_TRUE(out.contains("export interface"));
    EXPECT_TRUE(out.contains("export type DriverProxy"));
    EXPECT_TRUE(out.contains("add"));
}

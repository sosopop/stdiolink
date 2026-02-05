/**
 * 3DVision HTTP API Driver
 *
 * 通过 HTTP 调用 3DVision 工业料仓监控系统的所有 API 接口
 * API 版本: 3.8.1
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

#include "http_client.h"
#include "websocket_client.h"

using namespace stdiolink;
using namespace stdiolink::meta;

static const QString DEFAULT_ADDR = "localhost:6100";

class Vision3DHandler : public IMetaCommandHandler {
public:
    Vision3DHandler();

    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    QString getBaseUrl(const QJsonObject& params);
    void sendResponse(const QJsonObject& apiResp, IResponder& resp);

    // User management
    void handleLogin(const QJsonObject& params, IResponder& resp);
    void handleUserList(const QJsonObject& params, IResponder& resp);
    void handleUserAdd(const QJsonObject& params, IResponder& resp);
    void handleUserDel(const QJsonObject& params, IResponder& resp);
    void handleUserDetail(const QJsonObject& params, IResponder& resp);
    void handleUserModify(const QJsonObject& params, IResponder& resp);
    void handleUserChangePassword(const QJsonObject& params, IResponder& resp);

    // Vessel management
    void handleVesselList(const QJsonObject& params, IResponder& resp);
    void handleVesselDetail(const QJsonObject& params, IResponder& resp);
    void handleVesselAdd(const QJsonObject& params, IResponder& resp);
    void handleVesselModify(const QJsonObject& params, IResponder& resp);
    void handleVesselDel(const QJsonObject& params, IResponder& resp);
    void handleVesselImport(const QJsonObject& params, IResponder& resp);
    void handleVesselClone(const QJsonObject& params, IResponder& resp);
    void handleVesselEnable(const QJsonObject& params, IResponder& resp);
    void handleVesselExists(const QJsonObject& params, IResponder& resp);
    void handleVesselCommand(const QJsonObject& params, IResponder& resp);

    // Vessel log
    void handleVessellogList(const QJsonObject& params, IResponder& resp);
    void handleVessellogLast(const QJsonObject& params, IResponder& resp);
    void handleVessellogLastAll(const QJsonObject& params, IResponder& resp);

    // Material management
    void handleMaterialList(const QJsonObject& params, IResponder& resp);
    void handleMaterialGet(const QJsonObject& params, IResponder& resp);
    void handleMaterialAdd(const QJsonObject& params, IResponder& resp);
    void handleMaterialDel(const QJsonObject& params, IResponder& resp);

    // Filter management
    void handleFilterList(const QJsonObject& params, IResponder& resp);
    void handleFilterDetail(const QJsonObject& params, IResponder& resp);
    void handleFilterReplace(const QJsonObject& params, IResponder& resp);
    void handleFilterDel(const QJsonObject& params, IResponder& resp);
    void handleFilterExists(const QJsonObject& params, IResponder& resp);

    // Platform operations
    void handlePlatformVersion(const QJsonObject& params, IResponder& resp);
    void handlePlatformConsole(const QJsonObject& params, IResponder& resp);
    void handlePlatformGuideInfo(const QJsonObject& params, IResponder& resp);
    void handlePlatformUploadModel(const QJsonObject& params, IResponder& resp);
    void handlePlatformBackupSystem(const QJsonObject& params, IResponder& resp);
    void handlePlatformRestoreSystem(const QJsonObject& params, IResponder& resp);
    void handlePlatformSettings(const QJsonObject& params, IResponder& resp);

    // Custom model
    void handleCustommodelUpload(const QJsonObject& params, IResponder& resp);
    void handleCustommodelList(const QJsonObject& params, IResponder& resp);
    void handleCustommodelDel(const QJsonObject& params, IResponder& resp);

    // WebSocket
    void handleWsConnect(const QJsonObject& params, IResponder& resp);
    void handleWsSubscribe(const QJsonObject& params, IResponder& resp);
    void handleWsUnsubscribe(const QJsonObject& params, IResponder& resp);
    void handleWsDisconnect(const QJsonObject& params, IResponder& resp);

    HttpClient m_client;
    WebSocketClient m_wsClient;
    DriverMeta m_meta;
    QString m_token;
    IResponder* m_wsResponder = nullptr;
};

// Helper to create addr parameter
static FieldBuilder addrParam() {
    return FieldBuilder("addr", FieldType::String)
        .defaultValue(DEFAULT_ADDR)
        .description("服务器地址 (host:port)");
}

// Helper to create token parameter
static FieldBuilder tokenParam() {
    return FieldBuilder("token", FieldType::String)
        .description("认证令牌 (从登录接口获取)");
}

Vision3DHandler::Vision3DHandler()
{
    buildMeta();

    // Connect WebSocket events to responder
    QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
        [this](const QString& eventName, const QJsonObject& data) {
            if (m_wsResponder) {
                m_wsResponder->event(eventName, 0, data);
            }
        });

    QObject::connect(&m_wsClient, &WebSocketClient::disconnected,
        [this]() {
            if (m_wsResponder) {
                m_wsResponder->event("ws.disconnected", 0, QJsonObject{});
            }
        });

    QObject::connect(&m_wsClient, &WebSocketClient::error,
        [this](const QString& msg) {
            if (m_wsResponder) {
                m_wsResponder->event("ws.error", 1, QJsonObject{{"message", msg}});
            }
        });
}

QString Vision3DHandler::getBaseUrl(const QJsonObject& params)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    if (!addr.startsWith("http://") && !addr.startsWith("https://")) {
        addr = "http://" + addr;
    }
    return addr;
}

void Vision3DHandler::sendResponse(const QJsonObject& apiResp, IResponder& resp)
{
    int code = apiResp["code"].toInt(-1);
    if (code == 0) {
        resp.done(0, apiResp["data"]);
    } else {
        QJsonObject errData;
        errData["message"] = apiResp["message"].toString();
        errData["apiCode"] = code;
        resp.error(code, errData);
    }
}

void Vision3DHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("3dvision.api", "3DVision API Driver", "1.0.0",
              "3D 视觉工业料仓监测系统的 HTTP API 驱动程序")
        .vendor("3DVision")

        // ========== User Management ==========
        .command(CommandBuilder("login")
            .title("用户登录")
            .description("用户登录以获取认证令牌")
            .group("用户管理")
            .param(addrParam())
            .param(FieldBuilder("userName", FieldType::String).required().description("用户名"))
            .param(FieldBuilder("password", FieldType::String).required().description("密码"))
            .param(FieldBuilder("viewMode", FieldType::Bool)
                .defaultValue(false)
                .description("查看模式，为 true 时返回观察者角色的匿名令牌"))
            .returns(FieldType::Object, "包含令牌(token)和角色(role)"))

        .command(CommandBuilder("user.list")
            .title("获取用户列表")
            .description("获取系统中的用户列表")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("offset", FieldType::Int).defaultValue(0).description("分页偏移量"))
            .param(FieldBuilder("count", FieldType::Int).defaultValue(1000).description("返回数量限制"))
            .returns(FieldType::Array, "用户列表"))

        .command(CommandBuilder("user.add")
            .title("创建新用户")
            .description("创建一个新的系统用户")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).description("显示名称"))
            .param(FieldBuilder("userName", FieldType::String).required().description("用户名"))
            .param(FieldBuilder("password", FieldType::String).required().description("密码"))
            .param(FieldBuilder("role", FieldType::Int).required()
                .description("用户角色 (0=管理员, 1=操作员, 2=观察者)"))
            .returns(FieldType::Object, "新用户的令牌和角色"))

        .command(CommandBuilder("user.del")
            .title("删除用户")
            .description("删除一个已存在的用户")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required().description("要删除的用户名"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("user.detail")
            .title("获取用户详情")
            .description("获取指定用户的详细信息")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required().description("用户名"))
            .returns(FieldType::Object, "用户信息"))

        .command(CommandBuilder("user.modify")
            .title("修改用户信息")
            .description("修改现有用户的信息")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required().description("要修改的用户名"))
            .param(FieldBuilder("name", FieldType::String).description("新的显示名称"))
            .param(FieldBuilder("password", FieldType::String).description("新密码"))
            .param(FieldBuilder("role", FieldType::Int).required().description("新角色"))
            .returns(FieldType::Object, "更新后的令牌和角色"))

        .command(CommandBuilder("user.changePassword")
            .title("修改用户密码")
            .description("修改当前用户的密码")
            .group("用户管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required().description("用户名"))
            .param(FieldBuilder("password", FieldType::String).required()
                .description("当前密码"))
            .param(FieldBuilder("newPassword", FieldType::String).required().description("新密码"))
            .returns(FieldType::Object, "新的令牌和角色"))

        // ========== Vessel Management ==========
        .command(CommandBuilder("vessel.list")
            .title("获取料仓列表")
            .description("获取所有料仓的列表")
            .group("料仓管理")
            .param(addrParam())
            .returns(FieldType::Array, "料仓列表"))

        .command(CommandBuilder("vessel.detail")
            .title("获取料仓详情")
            .description("获取单个料仓的详细信息")
            .group("料仓管理")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .returns(FieldType::Object, "料仓详细信息 (VesselInfo)"))

        .command(CommandBuilder("vessel.add")
            .title("创建新料仓")
            .description("创建一个新的料仓")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required()
                .description("料仓信息对象 (VesselInfo)"))
            .returns(FieldType::Object, "创建的料仓ID和名称"))

        .command(CommandBuilder("vessel.modify")
            .title("修改料仓配置")
            .description("修改现有料仓的配置")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required()
                .description("带有ID的料仓信息对象 (VesselInfo)"))
            .returns(FieldType::Object, "修改后的料仓ID和名称"))

        .command(CommandBuilder("vessel.del")
            .title("删除料仓")
            .description("删除一个已存在的料仓")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("vessel.import")
            .title("导入料仓配置")
            .description("导入料仓配置")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required().description("料仓信息对象"))
            .returns(FieldType::Object, "导入后的料仓ID和名称"))

        .command(CommandBuilder("vessel.clone")
            .title("克隆料仓")
            .description("克隆现有的料仓")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required().description("料仓信息对象"))
            .returns(FieldType::Object, "克隆后的料仓ID和名称"))

        .command(CommandBuilder("vessel.enable")
            .title("启用或禁用料仓")
            .description("启用或禁用指定的料仓")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .param(FieldBuilder("enable", FieldType::Bool).required().description("true=启用, false=禁用"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("vessel.exists")
            .title("检查料仓名称是否存在")
            .description("检查指定的料仓名称是否已存在")
            .group("料仓管理")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required().description("料仓名称"))
            .returns(FieldType::Object, "包含 exists 布尔值"))

        .command(CommandBuilder("vessel.command")
            .title("执行料仓命令")
            .description("对指定料仓执行命令")
            .group("料仓管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .param(FieldBuilder("cmd", FieldType::String).required()
                .description("命令类型: scan(扫描)/crane_pull_in(起重机收回)/crane_push_out(起重机伸出)/get_vessel_info(获取信息)"))
            .returns(FieldType::Object, "命令执行结果"))

        // ========== Vessel Log ==========
        .command(CommandBuilder("vessellog.list")
            .title("查询料仓历史日志")
            .description("查询料仓的历史日志记录")
            .group("料仓日志")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .param(FieldBuilder("beginTime", FieldType::String)
                .description("开始时间 (格式: YYYY-MM-DD HH:mm:ss)"))
            .param(FieldBuilder("endTime", FieldType::String)
                .description("结束时间 (格式: YYYY-MM-DD HH:mm:ss)"))
            .param(FieldBuilder("count", FieldType::Int).required().description("返回数量限制"))
            .param(FieldBuilder("offset", FieldType::Int).required().description("分页偏移量"))
            .param(FieldBuilder("desc", FieldType::Bool).defaultValue(true).description("是否降序排列 (true=最新的在前)"))
            .returns(FieldType::Array, "日志列表"))

        .command(CommandBuilder("vessellog.last")
            .title("获取最新日志")
            .description("获取单个料仓的最新日志记录")
            .group("料仓日志")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("料仓ID"))
            .returns(FieldType::Object, "料仓日志信息 (VesselLogInfo)"))

        .command(CommandBuilder("vessellog.lastAll")
            .title("批量获取最新日志")
            .description("批量获取多个料仓的最新日志记录")
            .group("料仓日志")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::String).required()
                .description("逗号分隔的料仓ID列表 (例如: 1,2,3)"))
            .returns(FieldType::Array, "日志列表"))

        // ========== Material Management ==========
        .command(CommandBuilder("material.list")
            .title("获取物料列表")
            .description("获取所有物料的列表")
            .group("物料管理")
            .param(addrParam())
            .returns(FieldType::Array, "物料列表"))

        .command(CommandBuilder("material.get")
            .title("获取物料详情")
            .description("获取单个物料的详细信息")
            .group("物料管理")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required().description("物料名称"))
            .returns(FieldType::Object, "物料信息"))

        .command(CommandBuilder("material.add")
            .title("创建或更新物料")
            .description("创建新物料或更新现有物料配置")
            .group("物料管理")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required().description("物料名称"))
            .param(FieldBuilder("densityType", FieldType::String)
                .description("密度类型: LevelDensityTable(液位密度表)/VolumeDensityTable(体积密度表)"))
            .param(FieldBuilder("densityTable", FieldType::Array).description("密度映射表"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("material.del")
            .title("删除物料")
            .description("删除指定的物料")
            .group("物料管理")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required().description("物料名称"))
            .returns(FieldType::Object, "成功时返回空对象"))

        // ========== Filter Management ==========
        .command(CommandBuilder("filter.list")
            .title("获取滤波器列表")
            .description("获取所有滤波器的列表")
            .group("滤波器管理")
            .param(addrParam())
            .param(tokenParam())
            .returns(FieldType::Array, "滤波器列表"))

        .command(CommandBuilder("filter.detail")
            .title("获取滤波器详情")
            .description("获取单个滤波器的详细配置")
            .group("滤波器管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required().description("滤波器名称"))
            .returns(FieldType::Object, "包含内容的滤波器信息"))

        .command(CommandBuilder("filter.replace")
            .title("创建或更新滤波器")
            .description("创建新滤波器或更新现有滤波器配置")
            .group("滤波器管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required().description("滤波器名称"))
            .param(FieldBuilder("predefined", FieldType::Bool).required().description("是否为预定义滤波器"))
            .param(FieldBuilder("content", FieldType::String).required().description("滤波器配置内容 (JSON字符串)"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("filter.del")
            .title("删除滤波器")
            .description("删除指定的滤波器")
            .group("滤波器管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required().description("滤波器名称"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("filter.exists")
            .title("检查滤波器是否存在")
            .description("检查指定的滤波器是否存在")
            .group("滤波器管理")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required().description("滤波器名称"))
            .returns(FieldType::Object, "包含 exists 布尔值"))

        // ========== Platform Operations ==========
        .command(CommandBuilder("platform.version")
            .title("获取系统版本")
            .description("获取系统的版本信息")
            .group("平台操作")
            .param(addrParam())
            .returns(FieldType::Object, "包含版本字符串"))

        .command(CommandBuilder("platform.console")
            .title("控制台窗口控制")
            .description("显示或隐藏控制台窗口 (仅 Windows)")
            .group("平台操作")
            .param(addrParam())
            .param(FieldBuilder("show", FieldType::Bool).required().description("true=显示, false=隐藏"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("platform.guideInfo")
            .title("获取引导信息")
            .description("获取系统初始化引导信息 (串口、滤波器等)")
            .group("平台操作")
            .param(addrParam())
            .returns(FieldType::Object, "串口、滤波器、分组名称等信息"))

        .command(CommandBuilder("platform.uploadModel")
            .title("上传3D模型")
            .description("上传 3D 模型文件 (OSG)")
            .group("平台操作")
            .param(addrParam())
            .param(FieldBuilder("extension", FieldType::String).defaultValue("osg").description("文件扩展名"))
            .param(FieldBuilder("data", FieldType::String).required()
                .description("Base64 编码的模型数据"))
            .returns(FieldType::Object, "哈希值和文件 URL"))

        .command(CommandBuilder("platform.backupSystem")
            .title("备份系统配置")
            .description("备份系统配置")
            .group("平台操作")
            .param(addrParam())
            .returns(FieldType::Object, "备份文件路径"))

        .command(CommandBuilder("platform.restoreSystem")
            .title("恢复系统配置")
            .description("从备份文件恢复系统配置")
            .group("平台操作")
            .param(addrParam())
            .param(FieldBuilder("path", FieldType::String).required().description("备份文件路径"))
            .returns(FieldType::Object, "成功时返回空对象"))

        .command(CommandBuilder("platform.settings")
            .title("获取系统设置")
            .description("获取系统设置信息")
            .group("平台操作")
            .param(addrParam())
            .returns(FieldType::Object, "设置和版本信息"))

        // ========== Custom Model ==========
        .command(CommandBuilder("custommodel.upload")
            .title("上传自定义模型")
            .description("上传自定义 3D 模型 (PLY)")
            .group("自定义模型")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required().description("模型名称"))
            .param(FieldBuilder("data", FieldType::String).required()
                .description("Base64 编码的 PLY 数据"))
            .returns(FieldType::Object, "模型ID、名称、哈希值"))

        .command(CommandBuilder("custommodel.list")
            .title("获取自定义模型列表")
            .description("获取所有自定义模型的列表")
            .group("自定义模型")
            .param(addrParam())
            .returns(FieldType::Array, "模型列表"))

        .command(CommandBuilder("custommodel.del")
            .title("删除自定义模型")
            .description("删除指定的自定义模型")
            .group("自定义模型")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required().description("模型ID"))
            .returns(FieldType::Object, "成功时返回空对象"))

        // ========== WebSocket ==========
        .command(CommandBuilder("ws.connect")
            .title("连接 WebSocket")
            .description("连接到 WebSocket 以接收实时事件")
            .group("WebSocket")
            .param(addrParam())
            .returns(FieldType::Object, "连接状态")
            .event("scanner.ready", "扫描仪就绪")
            .event("scanner.scanning", "正在扫描")
            .event("scanner.progress", "扫描进度")
            .event("scanner.result", "扫描结果")
            .event("scanner.error", "扫描仪错误")
            .event("scanner.event", "事件日志")
            .event("scanner.created", "料仓创建通知")
            .event("scanner.modified", "料仓修改通知")
            .event("scanner.deleted", "料仓删除通知")
            .event("ws.disconnected", "WebSocket 已断开")
            .event("ws.error", "WebSocket 错误"))

        .command(CommandBuilder("ws.subscribe")
            .title("订阅事件")
            .description("订阅指定的事件主题")
            .group("WebSocket")
            .param(FieldBuilder("topic", FieldType::String)
                .required()
                .defaultValue("vessel.notify")
                .description("主题名称"))
            .returns(FieldType::Object, "订阅状态"))

        .command(CommandBuilder("ws.unsubscribe")
            .title("取消订阅")
            .description("取消订阅指定的事件主题")
            .group("WebSocket")
            .param(FieldBuilder("topic", FieldType::String).required().description("主题名称"))
            .returns(FieldType::Object, "取消订阅状态"))

        .command(CommandBuilder("ws.disconnect")
            .title("断开 WebSocket")
            .description("断开 WebSocket 连接")
            .group("WebSocket")
            .returns(FieldType::Object, "断开状态"))

        .build();
}

void Vision3DHandler::handle(const QString& cmd, const QJsonValue& data,
                             IResponder& resp)
{
    QJsonObject params = data.toObject();

    // User management
    if (cmd == "login") { handleLogin(params, resp); return; }
    if (cmd == "user.list") { handleUserList(params, resp); return; }
    if (cmd == "user.add") { handleUserAdd(params, resp); return; }
    if (cmd == "user.del") { handleUserDel(params, resp); return; }
    if (cmd == "user.detail") { handleUserDetail(params, resp); return; }
    if (cmd == "user.modify") { handleUserModify(params, resp); return; }
    if (cmd == "user.changePassword") { handleUserChangePassword(params, resp); return; }

    // Vessel management
    if (cmd == "vessel.list") { handleVesselList(params, resp); return; }
    if (cmd == "vessel.detail") { handleVesselDetail(params, resp); return; }
    if (cmd == "vessel.add") { handleVesselAdd(params, resp); return; }
    if (cmd == "vessel.modify") { handleVesselModify(params, resp); return; }
    if (cmd == "vessel.del") { handleVesselDel(params, resp); return; }
    if (cmd == "vessel.import") { handleVesselImport(params, resp); return; }
    if (cmd == "vessel.clone") { handleVesselClone(params, resp); return; }
    if (cmd == "vessel.enable") { handleVesselEnable(params, resp); return; }
    if (cmd == "vessel.exists") { handleVesselExists(params, resp); return; }
    if (cmd == "vessel.command") { handleVesselCommand(params, resp); return; }

    // Vessel log
    if (cmd == "vessellog.list") { handleVessellogList(params, resp); return; }
    if (cmd == "vessellog.last") { handleVessellogLast(params, resp); return; }
    if (cmd == "vessellog.lastAll") { handleVessellogLastAll(params, resp); return; }

    // Material management
    if (cmd == "material.list") { handleMaterialList(params, resp); return; }
    if (cmd == "material.get") { handleMaterialGet(params, resp); return; }
    if (cmd == "material.add") { handleMaterialAdd(params, resp); return; }
    if (cmd == "material.del") { handleMaterialDel(params, resp); return; }

    // Filter management
    if (cmd == "filter.list") { handleFilterList(params, resp); return; }
    if (cmd == "filter.detail") { handleFilterDetail(params, resp); return; }
    if (cmd == "filter.replace") { handleFilterReplace(params, resp); return; }
    if (cmd == "filter.del") { handleFilterDel(params, resp); return; }
    if (cmd == "filter.exists") { handleFilterExists(params, resp); return; }

    // Platform operations
    if (cmd == "platform.version") { handlePlatformVersion(params, resp); return; }
    if (cmd == "platform.console") { handlePlatformConsole(params, resp); return; }
    if (cmd == "platform.guideInfo") { handlePlatformGuideInfo(params, resp); return; }
    if (cmd == "platform.uploadModel") { handlePlatformUploadModel(params, resp); return; }
    if (cmd == "platform.backupSystem") { handlePlatformBackupSystem(params, resp); return; }
    if (cmd == "platform.restoreSystem") { handlePlatformRestoreSystem(params, resp); return; }
    if (cmd == "platform.settings") { handlePlatformSettings(params, resp); return; }

    // Custom model
    if (cmd == "custommodel.upload") { handleCustommodelUpload(params, resp); return; }
    if (cmd == "custommodel.list") { handleCustommodelList(params, resp); return; }
    if (cmd == "custommodel.del") { handleCustommodelDel(params, resp); return; }

    // WebSocket
    if (cmd == "ws.connect") { handleWsConnect(params, resp); return; }
    if (cmd == "ws.subscribe") { handleWsSubscribe(params, resp); return; }
    if (cmd == "ws.unsubscribe") { handleWsUnsubscribe(params, resp); return; }
    if (cmd == "ws.disconnect") { handleWsDisconnect(params, resp); return; }

    resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

// ========== User Management Handlers ==========

void Vision3DHandler::handleLogin(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["userName"] = params["userName"];
    body["password"] = params["password"];
    if (params.contains("viewMode")) {
        body["viewMode"] = params["viewMode"];
    }
    QJsonObject result = m_client.post("/api/user/login", body);
    if (result["code"].toInt() == 0) {
        m_token = result["data"].toObject()["token"].toString();
        m_client.setToken(m_token);
    }
    sendResponse(result, resp);
}

void Vision3DHandler::handleUserList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["offset"] = params["offset"].toInt(0);
    body["count"] = params["count"].toInt(1000);
    sendResponse(m_client.post("/api/user/list", body), resp);
}

void Vision3DHandler::handleUserAdd(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    if (params.contains("name")) body["name"] = params["name"];
    body["userName"] = params["userName"];
    body["password"] = params["password"];
    body["role"] = params["role"];
    sendResponse(m_client.post("/api/user/add", body), resp);
}

void Vision3DHandler::handleUserDel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    sendResponse(m_client.post("/api/user/del", body), resp);
}

void Vision3DHandler::handleUserDetail(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    sendResponse(m_client.post("/api/user/detail", body), resp);
}

void Vision3DHandler::handleUserModify(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    if (params.contains("name")) body["name"] = params["name"];
    if (params.contains("password")) body["password"] = params["password"];
    body["role"] = params["role"];
    sendResponse(m_client.post("/api/user/modify", body), resp);
}

void Vision3DHandler::handleUserChangePassword(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    body["password"] = params["password"];
    body["newPassword"] = params["newPassword"];
    sendResponse(m_client.post("/api/user/change-password", body), resp);
}

// ========== Vessel Management Handlers ==========

void Vision3DHandler::handleVesselList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/vessel/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleVesselDetail(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessel/detail", body), resp);
}

void Vision3DHandler::handleVesselAdd(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/add", vessel), resp);
}

void Vision3DHandler::handleVesselModify(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/modify", vessel), resp);
}

void Vision3DHandler::handleVesselDel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessel/del", body), resp);
}

void Vision3DHandler::handleVesselImport(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/import", vessel), resp);
}

void Vision3DHandler::handleVesselClone(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/clone", vessel), resp);
}

void Vision3DHandler::handleVesselEnable(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    body["enable"] = params["enable"];
    sendResponse(m_client.post("/api/vessel/enable", body), resp);
}

void Vision3DHandler::handleVesselExists(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/vessel/exists", body), resp);
}

void Vision3DHandler::handleVesselCommand(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    body["cmd"] = params["cmd"];
    sendResponse(m_client.post("/api/vessel/command", body), resp);
}

// ========== Vessel Log Handlers ==========

void Vision3DHandler::handleVessellogList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    if (params.contains("beginTime")) body["beginTime"] = params["beginTime"];
    if (params.contains("endTime")) body["endTime"] = params["endTime"];
    body["count"] = params["count"];
    body["offset"] = params["offset"];
    body["desc"] = params["desc"].toBool(true);
    sendResponse(m_client.post("/api/vessellog/list", body), resp);
}

void Vision3DHandler::handleVessellogLast(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessellog/last", body), resp);
}

void Vision3DHandler::handleVessellogLastAll(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessellog/last-all", body), resp);
}

// ========== Material Management Handlers ==========

void Vision3DHandler::handleMaterialList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/material/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleMaterialGet(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/material/get", body), resp);
}

void Vision3DHandler::handleMaterialAdd(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    if (params.contains("densityType")) body["densityType"] = params["densityType"];
    if (params.contains("densityTable")) body["densityTable"] = params["densityTable"];
    sendResponse(m_client.post("/api/material/add", body), resp);
}

void Vision3DHandler::handleMaterialDel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/material/del", body), resp);
}

// ========== Filter Management Handlers ==========

void Vision3DHandler::handleFilterList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    sendResponse(m_client.post("/api/filter/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleFilterDetail(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/detail", body), resp);
}

void Vision3DHandler::handleFilterReplace(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    body["predefined"] = params["predefined"];
    body["content"] = params["content"];
    sendResponse(m_client.post("/api/filter/replace", body), resp);
}

void Vision3DHandler::handleFilterDel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/del", body), resp);
}

void Vision3DHandler::handleFilterExists(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token")) m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/exists", body), resp);
}

// ========== Platform Operations Handlers ==========

void Vision3DHandler::handlePlatformVersion(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/version", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformConsole(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["show"] = params["show"];
    sendResponse(m_client.post("/api/platform/console", body), resp);
}

void Vision3DHandler::handlePlatformGuideInfo(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/guide-info", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformUploadModel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QString ext = params["extension"].toString("osg");
    QByteArray data = QByteArray::fromBase64(params["data"].toString().toUtf8());
    sendResponse(m_client.postBinary("/api/platform/upload-model", data,
                                     "extension=" + ext), resp);
}

void Vision3DHandler::handlePlatformBackupSystem(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/backup-system", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformRestoreSystem(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["path"] = params["path"];
    sendResponse(m_client.post("/api/platform/restore-system", body), resp);
}

void Vision3DHandler::handlePlatformSettings(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/settings", QJsonObject{}), resp);
}

// ========== Custom Model Handlers ==========

void Vision3DHandler::handleCustommodelUpload(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QString name = params["name"].toString();
    QByteArray data = QByteArray::fromBase64(params["data"].toString().toUtf8());
    sendResponse(m_client.postBinary("/api/custommodel/upload", data,
                                     "name=" + QUrl::toPercentEncoding(name)), resp);
}

void Vision3DHandler::handleCustommodelList(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/custommodel/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleCustommodelDel(const QJsonObject& params, IResponder& resp)
{
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/custommodel/del", body), resp);
}

// ========== WebSocket Handlers ==========

void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = "ws://" + addr + "/ws";

    m_wsResponder = &resp;

    if (m_wsClient.connectToServer(wsUrl)) {
        resp.done(0, QJsonObject{
            {"connected", true},
            {"url", wsUrl}
        });
    } else {
        resp.error(1, QJsonObject{{"message", "Failed to connect"}});
    }
}

void Vision3DHandler::handleWsSubscribe(const QJsonObject& params, IResponder& resp)
{
    if (!m_wsClient.isConnected()) {
        resp.error(1, QJsonObject{{"message", "WebSocket not connected"}});
        return;
    }
    QString topic = params["topic"].toString("vessel.notify");
    m_wsClient.subscribe(topic);
    resp.done(0, QJsonObject{
        {"subscribed", true},
        {"topic", topic}
    });
}

void Vision3DHandler::handleWsUnsubscribe(const QJsonObject& params, IResponder& resp)
{
    if (!m_wsClient.isConnected()) {
        resp.error(1, QJsonObject{{"message", "WebSocket not connected"}});
        return;
    }
    QString topic = params["topic"].toString();
    m_wsClient.unsubscribe(topic);
    resp.done(0, QJsonObject{
        {"unsubscribed", true},
        {"topic", topic}
    });
}

void Vision3DHandler::handleWsDisconnect(const QJsonObject& params, IResponder& resp)
{
    Q_UNUSED(params)
    m_wsClient.disconnect();
    m_wsResponder = nullptr;
    resp.done(0, QJsonObject{{"disconnected", true}});
}

// ========== Main ==========

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Vision3DHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
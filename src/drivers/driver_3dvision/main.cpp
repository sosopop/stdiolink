/**
 * 3DVision HTTP API Driver
 *
 * 通过 HTTP 调用 3DVision 工业料仓监控系统的所有 API 接口
 * API 版本: 3.8.1
 */

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/stdio_responder.h"

#include "http_client.h"
#include "websocket_client.h"

using namespace stdiolink;
using namespace stdiolink::meta;

static const QString DEFAULT_ADDR = "localhost:6100";

class Vision3DHandler : public IMetaCommandHandler {
public:
    Vision3DHandler();

    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override;

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
    bool m_wsConnecting = false; // ws.connect 重入防护标志
};

// 共通参数 helper
static FieldBuilder addrParam() {
    return FieldBuilder("addr", FieldType::String)
        .defaultValue(DEFAULT_ADDR)
        .description("3DVision 服务地址，格式 host:port，如 127.0.0.1:6100");
}

static FieldBuilder tokenParam() {
    return FieldBuilder("token", FieldType::String)
        .description("认证令牌，从 login 命令返回值中获取");
}

// 根据当前日期生成示例时间范围
static QString exampleEndTime() {
    return QDate::currentDate().toString("yyyy-MM-dd") + " 23:59:59";
}
static QString exampleBeginTime() {
    return QDate::currentDate().addMonths(-1).toString("yyyy-MM-dd") + " 00:00:00";
}

Vision3DHandler::Vision3DHandler() {
    buildMeta();

    // 事件回调：提取业务 data，使用临时 StdioResponder（消除悬空指针 + 修正双层包装）
    QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
                     [](const QString& eventName, const QJsonObject& eventObj) {
                         if (eventName.isEmpty())
                             return; // 防御（虽然 client 层已过滤）
                         const QJsonValue payload = eventObj.contains("data")
                                                        ? eventObj.value("data")
                                                        : QJsonValue(eventObj);
                         StdioResponder().event(eventName, 0, payload);
                     });

    QObject::connect(&m_wsClient, &WebSocketClient::disconnected,
                     []() { StdioResponder().event("ws.disconnected", 0, QJsonObject{}); });

    QObject::connect(&m_wsClient, &WebSocketClient::error, [](const QString& msg) {
        StdioResponder().event("ws.error", 1, QJsonObject{{"message", msg}});
    });

    // Bug 3 修复：连接 connected 信号
    QObject::connect(&m_wsClient, &WebSocketClient::connected,
                     []() { qDebug() << "WebSocket connected"; });
}

QString Vision3DHandler::getBaseUrl(const QJsonObject& params) {
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    if (!addr.startsWith("http://") && !addr.startsWith("https://")) {
        addr = "http://" + addr;
    }
    return addr;
}

void Vision3DHandler::sendResponse(const QJsonObject& apiResp, IResponder& resp) {
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

void Vision3DHandler::buildMeta() {
    m_meta =
        DriverMetaBuilder()
            .schemaVersion("1.0")
            .info("3dvision.api", "智慧仓储管理系统CLI", "1.0.0",
                  "智慧仓储管理系统的 HTTP API 驱动程序")
            .vendor("3DVision")

            // ========== User Management ==========
            .command(
                CommandBuilder("login")
                    .title("用户登录")
                    .description("用户登录以获取认证令牌")
                    .group("用户管理")
                    .param(addrParam())
                    .param(FieldBuilder("userName", FieldType::String)
                               .required()
                               .description("登录用户名，如 admin"))
                    .param(FieldBuilder("password", FieldType::String)
                               .required()
                               .description("登录密码"))
                    .param(FieldBuilder("viewMode", FieldType::Bool)
                               .defaultValue(false)
                               .description("设为 true 跳过凭据校验，返回只读匿名令牌"))
                    .returns(FieldType::Object, "包含令牌(token)和角色(role)")
                    .example("使用管理员账号登录", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"userName", "admin"},
                                         {"password", "123456"}}))

            .command(CommandBuilder("user.list")
                         .title("获取用户列表")
                         .description("获取系统中的用户列表")
                         .group("用户管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("offset", FieldType::Int)
                                    .defaultValue(0)
                                    .description("从第几条开始（0-based），默认 0"))
                         .param(FieldBuilder("count", FieldType::Int)
                                    .defaultValue(1000)
                                    .description("最多返回多少条，默认 1000"))
                         .returns(FieldType::Array, "用户列表")
                         .example("获取前 50 个用户", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"offset", 0},
                                              {"count", 50}}))

            .command(
                CommandBuilder("user.add")
                    .title("创建新用户")
                    .description("创建一个新的系统用户")
                    .group("用户管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .description("用户显示名称，如 张三"))
                    .param(FieldBuilder("userName", FieldType::String)
                               .required()
                               .description("登录用户名（必须唯一）"))
                    .param(FieldBuilder("password", FieldType::String)
                               .required()
                               .description("登录密码"))
                    .param(FieldBuilder("role", FieldType::Int)
                               .required()
                               .description("角色编号：0=管理员(完全权限) / 1=操作员(可操作不可管用户) / "
                                             "2=观察者(只读)"))
                    .returns(FieldType::Object, "新用户的令牌和角色")
                    .example("创建操作员用户", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"token", "abc123"},
                                         {"name", "张三"},
                                         {"userName", "zhangsan"},
                                         {"password", "123456"},
                                         {"role", 1}}))

            .command(CommandBuilder("user.del")
                         .title("删除用户")
                         .description("删除一个已存在的用户")
                         .group("用户管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("userName", FieldType::String)
                                    .required()
                                    .description("要删除的用户名"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("删除用户 zhangsan", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"userName", "zhangsan"}}))

            .command(CommandBuilder("user.detail")
                         .title("获取用户详情")
                         .description("获取指定用户的详细信息")
                         .group("用户管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("userName", FieldType::String)
                                    .required()
                                    .description("要查询的用户名"))
                         .returns(FieldType::Object, "用户信息（id/name/userName/role）")
                         .example("查看 admin 用户详情", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"userName", "admin"}}))

            .command(
                CommandBuilder("user.modify")
                    .title("修改用户信息")
                    .description("修改现有用户的信息")
                    .group("用户管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("userName", FieldType::String)
                               .required()
                               .description("要修改的用户名"))
                    .param(FieldBuilder("name", FieldType::String)
                               .description("新的显示名称"))
                    .param(FieldBuilder("password", FieldType::String)
                               .description("新密码，留空则不修改密码"))
                    .param(FieldBuilder("role", FieldType::Int)
                               .required()
                               .description("新角色编号：0=管理员 / 1=操作员 / 2=观察者"))
                    .returns(FieldType::Object, "更新后的令牌和角色")
                    .example("将用户角色改为观察者", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"token", "abc123"},
                                         {"userName", "zhangsan"},
                                         {"role", 2}}))

            .command(CommandBuilder("user.changePassword")
                         .title("修改用户密码")
                         .description("修改当前用户的密码")
                         .group("用户管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("userName", FieldType::String)
                                    .required()
                                    .description("要修改密码的用户名"))
                         .param(FieldBuilder("password", FieldType::String)
                                    .required()
                                    .description("当前密码（需验证）"))
                         .param(FieldBuilder("newPassword", FieldType::String)
                                    .required()
                                    .description("新密码"))
                         .returns(FieldType::Object, "新的令牌和角色")
                         .example("修改 admin 密码", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"userName", "admin"},
                                              {"password", "123456"},
                                              {"newPassword", "newpass789"}}))

            // ========== Vessel Management ==========
            .command(CommandBuilder("vessel.list")
                         .title("获取料仓列表")
                         .description("获取所有料仓的列表")
                         .group("料仓管理")
                         .param(addrParam())
                         .returns(FieldType::Array, "料仓列表（VesselInfo 数组）")
                         .example("列出所有料仓", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(CommandBuilder("vessel.detail")
                         .title("获取料仓详情")
                         .description("获取单个料仓的详细信息")
                         .group("料仓管理")
                         .param(addrParam())
                         .param(FieldBuilder("id", FieldType::Int)
                                    .required()
                                    .description("料仓ID，从 vessel.list 返回值中获取"))
                         .returns(FieldType::Object, "料仓详细信息（VesselInfo）")
                         .example("查看料仓 1 的详情", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}, {"id", 1}}))

            .command(
                CommandBuilder("vessel.add")
                    .title("创建新料仓")
                    .description("创建一个新的料仓")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("vessel", FieldType::Object)
                               .required()
                               .description("VesselInfo 对象，必填字段: name, deviceType, "
                                            "port, middleShape, middleDiameter, middleHeight"))
                    .returns(FieldType::Object, "创建的料仓ID和名称")
                    .example("创建一个激光扫描料仓", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"token", "abc123"},
                                 {"vessel", QJsonObject{{"name", "料仓-001"},
                                                        {"deviceType", "laser"},
                                                        {"port", "COM1"},
                                                        {"middleShape", "Rcylinder"},
                                                        {"middleDiameter", 10.0},
                                                        {"middleHeight", 15.0}}}}))

            .command(
                CommandBuilder("vessel.modify")
                    .title("修改料仓配置")
                    .description("修改现有料仓的配置")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("vessel", FieldType::Object)
                               .required()
                               .description("VesselInfo 对象，必须包含 id 字段以标识要修改的料仓"))
                    .returns(FieldType::Object, "修改后的料仓ID和名称")
                    .example("修改料仓 1 的中部直径", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"token", "abc123"},
                                 {"vessel", QJsonObject{{"id", 1},
                                                        {"name", "料仓-001"},
                                                        {"deviceType", "laser"},
                                                        {"port", "COM1"},
                                                        {"middleShape", "Rcylinder"},
                                                        {"middleDiameter", 12.0},
                                                        {"middleHeight", 15.0}}}}))

            .command(CommandBuilder("vessel.del")
                         .title("删除料仓")
                         .description("删除一个已存在的料仓（不可逆，会同时删除历史日志）")
                         .group("料仓管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("id", FieldType::Int)
                                    .required()
                                    .description("要删除的料仓ID"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("删除料仓 2", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"id", 2}}))

            .command(
                CommandBuilder("vessel.import")
                    .title("导入料仓配置")
                    .description("导入已有的料仓配置（系统自动分配新ID）")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("vessel", FieldType::Object)
                               .required()
                               .description("完整的 VesselInfo 对象，同 vessel.add"))
                    .returns(FieldType::Object, "导入后的料仓ID和名称")
                    .example("导入料仓配置", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"token", "abc123"},
                                 {"vessel", QJsonObject{{"name", "导入的料仓"},
                                                        {"deviceType", "laser"},
                                                        {"port", "COM2"},
                                                        {"middleShape", "Rcylinder"},
                                                        {"middleDiameter", 10.0},
                                                        {"middleHeight", 15.0}}}}))

            .command(
                CommandBuilder("vessel.clone")
                    .title("克隆料仓")
                    .description("克隆现有的料仓（通常先用 vessel.detail 获取配置再修改 name）")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("vessel", FieldType::Object)
                               .required()
                               .description("VesselInfo 对象，name 必须与原料仓不同"))
                    .returns(FieldType::Object, "克隆后的料仓ID和名称")
                    .example("克隆料仓-001", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"token", "abc123"},
                                 {"vessel", QJsonObject{{"name", "料仓-001-副本"},
                                                        {"deviceType", "laser"},
                                                        {"port", "COM1"},
                                                        {"middleShape", "Rcylinder"},
                                                        {"middleDiameter", 10.0},
                                                        {"middleHeight", 15.0}}}}))

            .command(CommandBuilder("vessel.enable")
                         .title("启用或禁用料仓")
                         .description("启用或禁用指定的料仓（禁用会停止自动扫描）")
                         .group("料仓管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("id", FieldType::Int)
                                    .required()
                                    .description("料仓ID"))
                         .param(FieldBuilder("enable", FieldType::Bool)
                                    .required()
                                    .description("true=启用（恢复自动扫描） / false=禁用（停止自动扫描）"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("禁用料仓 1", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"id", 1},
                                              {"enable", false}}))

            .command(
                CommandBuilder("vessel.exists")
                    .title("检查料仓名称是否存在")
                    .description("创建料仓前检查名称是否已被占用（区分大小写）")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("要检查的料仓名称"))
                    .returns(FieldType::Object, "包含 exists 布尔值：true=已存在 / false=可用")
                    .example("检查名称是否已存在", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"name", "料仓-001"}}))

            .command(
                CommandBuilder("vessel.command")
                    .title("执行料仓命令")
                    .description("对指定料仓执行操作命令")
                    .group("料仓管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("id", FieldType::Int)
                               .required()
                               .description("目标料仓ID"))
                    .param(FieldBuilder("cmd", FieldType::String)
                               .required()
                               .description("命令类型: scan(触发一次扫描) / "
                                            "crane_pull_in(收回起重臂) / "
                                            "crane_push_out(伸出起重臂) / "
                                            "get_vessel_info(查询实时状态)"))
                    .returns(FieldType::Object, "命令执行结果")
                    .example("触发料仓 1 执行扫描", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"token", "abc123"},
                                         {"id", 1},
                                         {"cmd", "scan"}}))

            // ========== Vessel Log ==========
            .command(
                CommandBuilder("vessellog.list")
                    .title("查询料仓历史日志")
                    .description("查询料仓的历史扫描记录，支持分页和时间过滤")
                    .group("料仓日志")
                    .param(addrParam())
                    .param(FieldBuilder("id", FieldType::Int)
                               .required()
                               .description("料仓ID"))
                    .param(FieldBuilder("beginTime", FieldType::String)
                               .description(
                                   "起始时间，格式 YYYY-MM-DD HH:mm:ss，不填则不限起始"))
                    .param(FieldBuilder("endTime", FieldType::String)
                               .description(
                                   "截止时间，格式 YYYY-MM-DD HH:mm:ss，不填则不限截止"))
                    .param(FieldBuilder("count", FieldType::Int)
                               .required()
                               .description("最多返回多少条记录"))
                    .param(FieldBuilder("offset", FieldType::Int)
                               .required()
                               .description("从第几条开始（0-based）"))
                    .param(FieldBuilder("desc", FieldType::Bool)
                               .defaultValue(true)
                               .description(
                                   "true=按时间倒序(最新在前) / false=正序(最早在前)，默认 true"))
                    .returns(FieldType::Array, "日志列表（含液位/体积/重量/点云路径）")
                    .example("查询料仓 1 最近一个月的扫描记录", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"id", 1},
                                         {"beginTime", exampleBeginTime()},
                                         {"endTime", exampleEndTime()},
                                         {"count", 50},
                                         {"offset", 0},
                                         {"desc", true}}))

            .command(CommandBuilder("vessellog.last")
                         .title("获取最新日志")
                         .description("获取单个料仓最近一次扫描的日志记录")
                         .group("料仓日志")
                         .param(addrParam())
                         .param(FieldBuilder("id", FieldType::Int)
                                    .required()
                                    .description("料仓ID"))
                         .returns(FieldType::Object, "最新日志（VesselLogInfo）")
                         .example("获取料仓 1 的最新扫描记录", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}, {"id", 1}}))

            .command(CommandBuilder("vessellog.lastAll")
                         .title("批量获取最新日志")
                         .description("一次查询多个料仓各自最近一次扫描记录")
                         .group("料仓日志")
                         .param(addrParam())
                         .param(FieldBuilder("id", FieldType::String)
                                    .required()
                                    .description("多个料仓ID用逗号分隔，如 1,2,3"))
                         .returns(FieldType::Array, "各料仓最新日志列表")
                         .example("批量查询料仓 1/2/3 的最新记录", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"id", "1,2,3"}}))

            // ========== Material Management ==========
            .command(CommandBuilder("material.list")
                         .title("获取物料列表")
                         .description("获取所有物料（含密度配置）的列表")
                         .group("物料管理")
                         .param(addrParam())
                         .returns(FieldType::Array, "物料列表（含名称/密度类型/密度表）")
                         .example("列出所有物料", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(
                CommandBuilder("material.get")
                    .title("获取物料详情")
                    .description("获取单个物料的详细信息")
                    .group("物料管理")
                    .param(addrParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("物料名称，如 小麦"))
                    .returns(FieldType::Object, "物料信息（含密度类型和密度表）")
                    .example("查看小麦的密度配置", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"name", "小麦"}}))

            .command(
                CommandBuilder("material.add")
                    .title("创建或更新物料")
                    .description("创建新物料或更新现有物料配置（同名则更新）")
                    .group("物料管理")
                    .param(addrParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("物料名称，如 玉米"))
                    .param(FieldBuilder("densityType", FieldType::String)
                               .description("密度计算方式: LevelDensityTable=按液位查表 / "
                                            "VolumeDensityTable=按体积查表"))
                    .param(FieldBuilder("densityTable", FieldType::Array)
                               .description("密度数据点数组，格式 [{level,density}] 或 "
                                            "[{volume,density}]，需与 densityType 匹配"))
                    .returns(FieldType::Object, "成功时返回空对象")
                    .example("创建按液位查表的玉米物料", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"name", "玉米"},
                                 {"densityType", "LevelDensityTable"},
                                 {"densityTable",
                                  QJsonArray{QJsonObject{{"level", 0}, {"density", 720}},
                                             QJsonObject{{"level", 5}, {"density", 730}},
                                             QJsonObject{{"level", 10}, {"density", 740}}}}}))

            .command(
                CommandBuilder("material.del")
                    .title("删除物料")
                    .description("删除指定的物料（不影响已使用该物料的料仓）")
                    .group("物料管理")
                    .param(addrParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("要删除的物料名称"))
                    .returns(FieldType::Object, "成功时返回空对象")
                    .example("删除玉米物料", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"name", "玉米"}}))

            // ========== Filter Management ==========
            .command(CommandBuilder("filter.list")
                         .title("获取滤波器列表")
                         .description("获取所有滤波器列表（不含配置内容，需用 filter.detail 获取）")
                         .group("滤波器管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .returns(FieldType::Array, "滤波器列表（name/predefined）")
                         .example("列出所有滤波器", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"}}))

            .command(CommandBuilder("filter.detail")
                         .title("获取滤波器详情")
                         .description("获取单个滤波器的完整配置内容")
                         .group("滤波器管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("name", FieldType::String)
                                    .required()
                                    .description("滤波器名称"))
                         .returns(FieldType::Object, "滤波器信息（含 content 配置内容）")
                         .example("查看默认滤波器配置", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"name", "默认滤波器"}}))

            .command(
                CommandBuilder("filter.replace")
                    .title("创建或更新滤波器")
                    .description("创建新滤波器或更新现有滤波器配置（同名则更新）")
                    .group("滤波器管理")
                    .param(addrParam())
                    .param(tokenParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("滤波器名称"))
                    .param(FieldBuilder("predefined", FieldType::Bool)
                               .required()
                               .description(
                                   "true=系统预定义滤波器(不可删) / false=用户自定义"))
                    .param(FieldBuilder("content", FieldType::String)
                               .required()
                               .description("JSON 格式的滤波器配置字符串，"
                                            "如 {\"filters\":[{\"type\":\"voxel\",\"size\":0.1}]}"))
                    .returns(FieldType::Object, "成功时返回空对象")
                    .example("创建自定义体素滤波器", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"token", "abc123"},
                                 {"name", "自定义滤波器"},
                                 {"predefined", false},
                                 {"content",
                                  "{\"filters\":[{\"type\":\"voxel\",\"size\":0.1}]}"}}))

            .command(CommandBuilder("filter.del")
                         .title("删除滤波器")
                         .description("删除指定的滤波器（不可逆）")
                         .group("滤波器管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("name", FieldType::String)
                                    .required()
                                    .description("要删除的滤波器名称"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("删除自定义滤波器", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"name", "自定义滤波器"}}))

            .command(CommandBuilder("filter.exists")
                         .title("检查滤波器是否存在")
                         .description("检查指定的滤波器是否存在")
                         .group("滤波器管理")
                         .param(addrParam())
                         .param(tokenParam())
                         .param(FieldBuilder("name", FieldType::String)
                                    .required()
                                    .description("要检查的滤波器名称"))
                         .returns(FieldType::Object,
                                  "包含 exists 布尔值：true=存在 / false=不存在")
                         .example("检查默认滤波器是否存在", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"token", "abc123"},
                                              {"name", "默认滤波器"}}))

            // ========== Platform Operations ==========
            .command(CommandBuilder("platform.version")
                         .title("获取系统版本")
                         .description("获取 3DVision 系统版本号")
                         .group("平台操作")
                         .param(addrParam())
                         .returns(FieldType::Object, "包含 version 字符串")
                         .example("查询系统版本", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(CommandBuilder("platform.console")
                         .title("控制台窗口控制")
                         .description("显示或隐藏服务端控制台窗口（仅 Windows 有效，用于调试）")
                         .group("平台操作")
                         .param(addrParam())
                         .param(FieldBuilder("show", FieldType::Bool)
                                    .required()
                                    .description("true=显示控制台 / false=隐藏控制台"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("显示控制台窗口", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"},
                                              {"show", true}}))

            .command(CommandBuilder("platform.guideInfo")
                         .title("获取引导信息")
                         .description("获取系统初始化引导信息（可用串口、滤波器列表、分组名称）")
                         .group("平台操作")
                         .param(addrParam())
                         .returns(FieldType::Object,
                                  "包含 serialports(串口列表), filters(滤波器), "
                                  "groupNames(分组名)")
                         .example("获取系统引导信息", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(
                CommandBuilder("platform.uploadModel")
                    .title("上传3D模型")
                    .description("上传 OSG 格式的 3D 模型文件")
                    .group("平台操作")
                    .param(addrParam())
                    .param(FieldBuilder("extension", FieldType::String)
                               .defaultValue("osg")
                               .description("模型文件扩展名，默认 osg"))
                    .param(FieldBuilder("data", FieldType::String)
                               .required()
                               .description("模型文件的 Base64 编码字符串（先读取二进制文件再编码）"))
                    .returns(FieldType::Object, "包含 hash(MD5哈希) 和 fileUrl(访问路径)")
                    .example("上传 OSG 模型", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"extension", "osg"},
                                         {"data", "base64_encoded_data..."}}))

            .command(CommandBuilder("platform.backupSystem")
                         .title("备份系统配置")
                         .description("备份所有料仓配置、用户数据、物料信息等")
                         .group("平台操作")
                         .param(addrParam())
                         .returns(FieldType::Object, "包含 path(备份文件完整路径)")
                         .example("备份系统", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(
                CommandBuilder("platform.restoreSystem")
                    .title("恢复系统配置")
                    .description("从备份文件恢复系统配置（会覆盖当前所有配置，建议先备份）")
                    .group("平台操作")
                    .param(addrParam())
                    .param(FieldBuilder("path", FieldType::String)
                               .required()
                               .description("备份 zip 文件的完整路径，如 "
                                            "D:/backup/3dvision_backup_2026-01-29.zip"))
                    .returns(FieldType::Object, "成功时返回空对象")
                    .example("从备份恢复系统", QStringList{"stdio", "console"},
                             QJsonObject{
                                 {"addr", "127.0.0.1:6100"},
                                 {"path", "D:/backup/3dvision_backup_2026-01-29.zip"}}))

            .command(CommandBuilder("platform.settings")
                         .title("获取系统设置")
                         .description("获取系统设置和版本信息")
                         .group("平台操作")
                         .param(addrParam())
                         .returns(FieldType::Object, "包含 settings(配置键值对) 和 version")
                         .example("查询系统设置", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            // ========== Custom Model ==========
            .command(
                CommandBuilder("custommodel.upload")
                    .title("上传自定义模型")
                    .description("上传 PLY 格式的自定义 3D 模型文件")
                    .group("自定义模型")
                    .param(addrParam())
                    .param(FieldBuilder("name", FieldType::String)
                               .required()
                               .description("模型显示名称，如 自定义模型1"))
                    .param(FieldBuilder("data", FieldType::String)
                               .required()
                               .description(
                                   "PLY 模型文件的 Base64 编码字符串（先读取 .ply 文件再编码）"))
                    .returns(FieldType::Object, "包含 id, name, hash(MD5)")
                    .example("上传 PLY 自定义模型", QStringList{"stdio", "console"},
                             QJsonObject{{"addr", "127.0.0.1:6100"},
                                         {"name", "自定义模型1"},
                                         {"data", "base64_encoded_ply_data..."}}))

            .command(CommandBuilder("custommodel.list")
                         .title("获取自定义模型列表")
                         .description("获取所有已上传的自定义模型列表")
                         .group("自定义模型")
                         .param(addrParam())
                         .returns(FieldType::Array, "模型列表（id/name/hash）")
                         .example("列出所有自定义模型", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(CommandBuilder("custommodel.del")
                         .title("删除自定义模型")
                         .description("删除指定的自定义模型（同时删除文件）")
                         .group("自定义模型")
                         .param(addrParam())
                         .param(FieldBuilder("id", FieldType::Int)
                                    .required()
                                    .description("要删除的模型ID"))
                         .returns(FieldType::Object, "成功时返回空对象")
                         .example("删除模型 1", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}, {"id", 1}}))

            // ========== WebSocket ==========
            .command(CommandBuilder("ws.connect")
                         .title("连接 WebSocket")
                         .description("连接到 3DVision WebSocket 以接收实时扫描事件")
                         .group("WebSocket")
                         .param(addrParam())
                         .returns(FieldType::Object, "包含 connected(bool) 和 url")
                         .event("scanner.ready", "扫描仪就绪，可以接受新任务")
                         .event("scanner.scanning", "正在扫描中")
                         .event("scanner.progress", "扫描进度更新（含 current/total）")
                         .event("scanner.result", "扫描结果就绪（含液位/体积/重量数据）")
                         .event("scanner.error", "扫描仪发生错误")
                         .event("scanner.event", "事件日志（含 information 描述）")
                         .event("scanner.created", "料仓被创建")
                         .event("scanner.modified", "料仓配置被修改")
                         .event("scanner.deleted", "料仓被删除")
                         .event("ws.disconnected", "WebSocket 连接已断开")
                         .event("ws.error", "WebSocket 通信错误")
                         .example("连接到本地 WebSocket", QStringList{"stdio", "console"},
                                  QJsonObject{{"addr", "127.0.0.1:6100"}}))

            .command(CommandBuilder("ws.subscribe")
                         .title("订阅事件")
                         .description("订阅指定的事件主题以接收实时推送")
                         .group("WebSocket")
                         .param(FieldBuilder("topic", FieldType::String)
                                    .required()
                                    .defaultValue("vessel.notify")
                                    .description("事件主题，目前仅支持 vessel.notify"
                                                 "（所有料仓扫描和状态事件）"))
                         .returns(FieldType::Object, "包含 subscribed(bool) 和 topic")
                         .example("订阅料仓事件", QStringList{"stdio", "console"},
                                  QJsonObject{{"topic", "vessel.notify"}}))

            .command(
                CommandBuilder("ws.unsubscribe")
                    .title("取消订阅")
                    .description("取消订阅指定的事件主题")
                    .group("WebSocket")
                    .param(FieldBuilder("topic", FieldType::String)
                               .required()
                               .description("要取消的事件主题名称，如 vessel.notify"))
                    .returns(FieldType::Object, "包含 unsubscribed(bool) 和 topic")
                    .example("取消订阅料仓事件", QStringList{"stdio", "console"},
                             QJsonObject{{"topic", "vessel.notify"}}))

            .command(CommandBuilder("ws.disconnect")
                         .title("断开 WebSocket")
                         .description("断开 WebSocket 连接并清空所有订阅")
                         .group("WebSocket")
                         .returns(FieldType::Object, "包含 disconnected(bool)")
                         .example("断开 WebSocket 连接", QStringList{"stdio", "console"}, QJsonObject{}))

            .build();
}

void Vision3DHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    QJsonObject params = data.toObject();

    // User management
    if (cmd == "login") {
        handleLogin(params, resp);
        return;
    }
    if (cmd == "user.list") {
        handleUserList(params, resp);
        return;
    }
    if (cmd == "user.add") {
        handleUserAdd(params, resp);
        return;
    }
    if (cmd == "user.del") {
        handleUserDel(params, resp);
        return;
    }
    if (cmd == "user.detail") {
        handleUserDetail(params, resp);
        return;
    }
    if (cmd == "user.modify") {
        handleUserModify(params, resp);
        return;
    }
    if (cmd == "user.changePassword") {
        handleUserChangePassword(params, resp);
        return;
    }

    // Vessel management
    if (cmd == "vessel.list") {
        handleVesselList(params, resp);
        return;
    }
    if (cmd == "vessel.detail") {
        handleVesselDetail(params, resp);
        return;
    }
    if (cmd == "vessel.add") {
        handleVesselAdd(params, resp);
        return;
    }
    if (cmd == "vessel.modify") {
        handleVesselModify(params, resp);
        return;
    }
    if (cmd == "vessel.del") {
        handleVesselDel(params, resp);
        return;
    }
    if (cmd == "vessel.import") {
        handleVesselImport(params, resp);
        return;
    }
    if (cmd == "vessel.clone") {
        handleVesselClone(params, resp);
        return;
    }
    if (cmd == "vessel.enable") {
        handleVesselEnable(params, resp);
        return;
    }
    if (cmd == "vessel.exists") {
        handleVesselExists(params, resp);
        return;
    }
    if (cmd == "vessel.command") {
        handleVesselCommand(params, resp);
        return;
    }

    // Vessel log
    if (cmd == "vessellog.list") {
        handleVessellogList(params, resp);
        return;
    }
    if (cmd == "vessellog.last") {
        handleVessellogLast(params, resp);
        return;
    }
    if (cmd == "vessellog.lastAll") {
        handleVessellogLastAll(params, resp);
        return;
    }

    // Material management
    if (cmd == "material.list") {
        handleMaterialList(params, resp);
        return;
    }
    if (cmd == "material.get") {
        handleMaterialGet(params, resp);
        return;
    }
    if (cmd == "material.add") {
        handleMaterialAdd(params, resp);
        return;
    }
    if (cmd == "material.del") {
        handleMaterialDel(params, resp);
        return;
    }

    // Filter management
    if (cmd == "filter.list") {
        handleFilterList(params, resp);
        return;
    }
    if (cmd == "filter.detail") {
        handleFilterDetail(params, resp);
        return;
    }
    if (cmd == "filter.replace") {
        handleFilterReplace(params, resp);
        return;
    }
    if (cmd == "filter.del") {
        handleFilterDel(params, resp);
        return;
    }
    if (cmd == "filter.exists") {
        handleFilterExists(params, resp);
        return;
    }

    // Platform operations
    if (cmd == "platform.version") {
        handlePlatformVersion(params, resp);
        return;
    }
    if (cmd == "platform.console") {
        handlePlatformConsole(params, resp);
        return;
    }
    if (cmd == "platform.guideInfo") {
        handlePlatformGuideInfo(params, resp);
        return;
    }
    if (cmd == "platform.uploadModel") {
        handlePlatformUploadModel(params, resp);
        return;
    }
    if (cmd == "platform.backupSystem") {
        handlePlatformBackupSystem(params, resp);
        return;
    }
    if (cmd == "platform.restoreSystem") {
        handlePlatformRestoreSystem(params, resp);
        return;
    }
    if (cmd == "platform.settings") {
        handlePlatformSettings(params, resp);
        return;
    }

    // Custom model
    if (cmd == "custommodel.upload") {
        handleCustommodelUpload(params, resp);
        return;
    }
    if (cmd == "custommodel.list") {
        handleCustommodelList(params, resp);
        return;
    }
    if (cmd == "custommodel.del") {
        handleCustommodelDel(params, resp);
        return;
    }

    // WebSocket
    if (cmd == "ws.connect") {
        handleWsConnect(params, resp);
        return;
    }
    if (cmd == "ws.subscribe") {
        handleWsSubscribe(params, resp);
        return;
    }
    if (cmd == "ws.unsubscribe") {
        handleWsUnsubscribe(params, resp);
        return;
    }
    if (cmd == "ws.disconnect") {
        handleWsDisconnect(params, resp);
        return;
    }

    resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

// ========== User Management Handlers ==========

void Vision3DHandler::handleLogin(const QJsonObject& params, IResponder& resp) {
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

void Vision3DHandler::handleUserList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["offset"] = params["offset"].toInt(0);
    body["count"] = params["count"].toInt(1000);
    sendResponse(m_client.post("/api/user/list", body), resp);
}

void Vision3DHandler::handleUserAdd(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    if (params.contains("name"))
        body["name"] = params["name"];
    body["userName"] = params["userName"];
    body["password"] = params["password"];
    body["role"] = params["role"];
    sendResponse(m_client.post("/api/user/add", body), resp);
}

void Vision3DHandler::handleUserDel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    sendResponse(m_client.post("/api/user/del", body), resp);
}

void Vision3DHandler::handleUserDetail(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    sendResponse(m_client.post("/api/user/detail", body), resp);
}

void Vision3DHandler::handleUserModify(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    if (params.contains("name"))
        body["name"] = params["name"];
    if (params.contains("password"))
        body["password"] = params["password"];
    body["role"] = params["role"];
    sendResponse(m_client.post("/api/user/modify", body), resp);
}

void Vision3DHandler::handleUserChangePassword(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["userName"] = params["userName"];
    body["password"] = params["password"];
    body["newPassword"] = params["newPassword"];
    sendResponse(m_client.post("/api/user/change-password", body), resp);
}

// ========== Vessel Management Handlers ==========

void Vision3DHandler::handleVesselList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/vessel/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleVesselDetail(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessel/detail", body), resp);
}

void Vision3DHandler::handleVesselAdd(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/add", vessel), resp);
}

void Vision3DHandler::handleVesselModify(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/modify", vessel), resp);
}

void Vision3DHandler::handleVesselDel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessel/del", body), resp);
}

void Vision3DHandler::handleVesselImport(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/import", vessel), resp);
}

void Vision3DHandler::handleVesselClone(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject vessel = params["vessel"].toObject();
    sendResponse(m_client.post("/api/vessel/clone", vessel), resp);
}

void Vision3DHandler::handleVesselEnable(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    body["enable"] = params["enable"];
    sendResponse(m_client.post("/api/vessel/enable", body), resp);
}

void Vision3DHandler::handleVesselExists(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/vessel/exists", body), resp);
}

void Vision3DHandler::handleVesselCommand(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["id"] = params["id"];
    body["cmd"] = params["cmd"];
    sendResponse(m_client.post("/api/vessel/command", body), resp);
}

// ========== Vessel Log Handlers ==========

void Vision3DHandler::handleVessellogList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    if (params.contains("beginTime"))
        body["beginTime"] = params["beginTime"];
    if (params.contains("endTime"))
        body["endTime"] = params["endTime"];
    body["count"] = params["count"];
    body["offset"] = params["offset"];
    body["desc"] = params["desc"].toBool(true);
    sendResponse(m_client.post("/api/vessellog/list", body), resp);
}

void Vision3DHandler::handleVessellogLast(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessellog/last", body), resp);
}

void Vision3DHandler::handleVessellogLastAll(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/vessellog/last-all", body), resp);
}

// ========== Material Management Handlers ==========

void Vision3DHandler::handleMaterialList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/material/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleMaterialGet(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/material/get", body), resp);
}

void Vision3DHandler::handleMaterialAdd(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    if (params.contains("densityType"))
        body["densityType"] = params["densityType"];
    if (params.contains("densityTable"))
        body["densityTable"] = params["densityTable"];
    sendResponse(m_client.post("/api/material/add", body), resp);
}

void Vision3DHandler::handleMaterialDel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/material/del", body), resp);
}

// ========== Filter Management Handlers ==========

void Vision3DHandler::handleFilterList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    sendResponse(m_client.post("/api/filter/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleFilterDetail(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/detail", body), resp);
}

void Vision3DHandler::handleFilterReplace(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    body["predefined"] = params["predefined"];
    body["content"] = params["content"];
    sendResponse(m_client.post("/api/filter/replace", body), resp);
}

void Vision3DHandler::handleFilterDel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/del", body), resp);
}

void Vision3DHandler::handleFilterExists(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    if (params.contains("token"))
        m_client.setToken(params["token"].toString());
    QJsonObject body;
    body["name"] = params["name"];
    sendResponse(m_client.post("/api/filter/exists", body), resp);
}

// ========== Platform Operations Handlers ==========

void Vision3DHandler::handlePlatformVersion(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/version", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformConsole(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["show"] = params["show"];
    sendResponse(m_client.post("/api/platform/console", body), resp);
}

void Vision3DHandler::handlePlatformGuideInfo(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/guide-info", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformUploadModel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QString ext = params["extension"].toString("osg");
    QByteArray data = QByteArray::fromBase64(params["data"].toString().toUtf8());
    sendResponse(m_client.postBinary("/api/platform/upload-model", data, "extension=" + ext), resp);
}

void Vision3DHandler::handlePlatformBackupSystem(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/backup-system", QJsonObject{}), resp);
}

void Vision3DHandler::handlePlatformRestoreSystem(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["path"] = params["path"];
    sendResponse(m_client.post("/api/platform/restore-system", body), resp);
}

void Vision3DHandler::handlePlatformSettings(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/platform/settings", QJsonObject{}), resp);
}

// ========== Custom Model Handlers ==========

void Vision3DHandler::handleCustommodelUpload(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QString name = params["name"].toString();
    QByteArray data = QByteArray::fromBase64(params["data"].toString().toUtf8());
    sendResponse(m_client.postBinary("/api/custommodel/upload", data,
                                     "name=" + QUrl::toPercentEncoding(name)),
                 resp);
}

void Vision3DHandler::handleCustommodelList(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    sendResponse(m_client.post("/api/custommodel/list", QJsonObject{}), resp);
}

void Vision3DHandler::handleCustommodelDel(const QJsonObject& params, IResponder& resp) {
    m_client.setBaseUrl(getBaseUrl(params));
    QJsonObject body;
    body["id"] = params["id"];
    sendResponse(m_client.post("/api/custommodel/del", body), resp);
}

// ========== WebSocket Handlers ==========

void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp) {
    // 重入防护：QEventLoop 期间主线程仍会处理 queued invoke
    if (m_wsConnecting) {
        resp.error(1, QJsonObject{{"message", "ws.connect already in progress"}});
        return;
    }

    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = "ws://" + addr + "/ws";

    // 已连接且同地址：直接返回 done，不进入 QEventLoop
    if (m_wsClient.isConnected() && m_wsClient.currentUrl() == wsUrl) {
        resp.done(0, QJsonObject{{"connected", true}, {"url", wsUrl}});
        return;
    }

    m_wsConnecting = true;

    QEventLoop loop;
    bool success = false;
    QString errorMsg;
    QTimer timer;
    timer.setSingleShot(true);

    auto connOk = QObject::connect(&m_wsClient, &WebSocketClient::connected, [&]() {
        success = true;
        loop.quit();
    });
    auto connErr = QObject::connect(&m_wsClient, &WebSocketClient::error, [&](const QString& err) {
        errorMsg = err;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        errorMsg = "WebSocket connect timeout (5s)";
        loop.quit();
    });

    m_wsClient.connectToServer(wsUrl);
    timer.start(5000);
    loop.exec();

    QObject::disconnect(connOk);
    QObject::disconnect(connErr);
    m_wsConnecting = false;

    if (success) {
        resp.done(0, QJsonObject{{"connected", true}, {"url", wsUrl}});
    } else {
        // 超时或失败：清理挂起的连接尝试，防止晚到的 connected 信号导致状态漂移
        m_wsClient.disconnect();
        resp.error(1, QJsonObject{{"message", errorMsg}});
    }
}

void Vision3DHandler::handleWsSubscribe(const QJsonObject& params, IResponder& resp) {
    if (!m_wsClient.isConnected()) {
        resp.error(1, QJsonObject{{"message", "WebSocket not connected"}});
        return;
    }
    QString topic = params["topic"].toString("vessel.notify");
    m_wsClient.subscribe(topic);
    resp.done(0, QJsonObject{{"subscribed", true}, {"topic", topic}});
}

void Vision3DHandler::handleWsUnsubscribe(const QJsonObject& params, IResponder& resp) {
    if (!m_wsClient.isConnected()) {
        resp.error(1, QJsonObject{{"message", "WebSocket not connected"}});
        return;
    }
    QString topic = params["topic"].toString();
    m_wsClient.unsubscribe(topic);
    resp.done(0, QJsonObject{{"unsubscribed", true}, {"topic", topic}});
}

void Vision3DHandler::handleWsDisconnect(const QJsonObject& params, IResponder& resp) {
    Q_UNUSED(params)
    m_wsClient.disconnect();
    resp.done(0, QJsonObject{{"disconnected", true}});
}

// ========== Main ==========

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    Vision3DHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}

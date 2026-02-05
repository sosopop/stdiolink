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
        .description("Server address (host:port)");
}

// Helper to create token parameter
static FieldBuilder tokenParam() {
    return FieldBuilder("token", FieldType::String)
        .description("Authentication token (from login)");
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
              "HTTP API driver for 3DVision industrial silo monitoring system")
        .vendor("3DVision")

        // ========== User Management ==========
        .command(CommandBuilder("login")
            .description("User login to get authentication token")
            .group("user")
            .param(addrParam())
            .param(FieldBuilder("userName", FieldType::String).required())
            .param(FieldBuilder("password", FieldType::String).required())
            .param(FieldBuilder("viewMode", FieldType::Bool)
                .defaultValue(false)
                .description("View mode returns observer role token"))
            .returns(FieldType::Object, "Contains token and role"))

        .command(CommandBuilder("user.list")
            .description("Get user list")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("offset", FieldType::Int).defaultValue(0))
            .param(FieldBuilder("count", FieldType::Int).defaultValue(1000))
            .returns(FieldType::Array, "User list"))

        .command(CommandBuilder("user.add")
            .description("Create new user")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).description("Display name"))
            .param(FieldBuilder("userName", FieldType::String).required())
            .param(FieldBuilder("password", FieldType::String).required())
            .param(FieldBuilder("role", FieldType::Int).required()
                .description("0=Admin, 1=Operator, 2=Observer"))
            .returns(FieldType::Object, "New user token and role"))

        .command(CommandBuilder("user.del")
            .description("Delete user")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("user.detail")
            .description("Get user details")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required())
            .returns(FieldType::Object, "User info"))

        .command(CommandBuilder("user.modify")
            .description("Modify user info")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required())
            .param(FieldBuilder("name", FieldType::String))
            .param(FieldBuilder("password", FieldType::String))
            .param(FieldBuilder("role", FieldType::Int).required())
            .returns(FieldType::Object, "Updated token and role"))

        .command(CommandBuilder("user.changePassword")
            .description("Change user password")
            .group("user")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("userName", FieldType::String).required())
            .param(FieldBuilder("password", FieldType::String).required()
                .description("Current password"))
            .param(FieldBuilder("newPassword", FieldType::String).required())
            .returns(FieldType::Object, "New token and role"))

        // ========== Vessel Management ==========
        .command(CommandBuilder("vessel.list")
            .description("Get all vessels list")
            .group("vessel")
            .param(addrParam())
            .returns(FieldType::Array, "Vessel list"))

        .command(CommandBuilder("vessel.detail")
            .description("Get vessel details")
            .group("vessel")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .returns(FieldType::Object, "VesselInfo"))

        .command(CommandBuilder("vessel.add")
            .description("Create new vessel")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required()
                .description("VesselInfo object"))
            .returns(FieldType::Object, "Created vessel id and name"))

        .command(CommandBuilder("vessel.modify")
            .description("Modify vessel configuration")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required()
                .description("VesselInfo object with id"))
            .returns(FieldType::Object, "Modified vessel id and name"))

        .command(CommandBuilder("vessel.del")
            .description("Delete vessel")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("vessel.import")
            .description("Import vessel configuration")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required())
            .returns(FieldType::Object, "Imported vessel id and name"))

        .command(CommandBuilder("vessel.clone")
            .description("Clone existing vessel")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("vessel", FieldType::Object).required())
            .returns(FieldType::Object, "Cloned vessel id and name"))

        .command(CommandBuilder("vessel.enable")
            .description("Enable or disable vessel")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .param(FieldBuilder("enable", FieldType::Bool).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("vessel.exists")
            .description("Check if vessel name exists")
            .group("vessel")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Contains exists boolean"))

        .command(CommandBuilder("vessel.command")
            .description("Execute vessel command")
            .group("vessel")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .param(FieldBuilder("cmd", FieldType::String).required()
                .description("scan/crane_pull_in/crane_push_out/get_vessel_info"))
            .returns(FieldType::Object, "Command result"))

        // ========== Vessel Log ==========
        .command(CommandBuilder("vessellog.list")
            .description("Query vessel history logs")
            .group("vessellog")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .param(FieldBuilder("beginTime", FieldType::String)
                .description("Start time (YYYY-MM-DD HH:mm:ss)"))
            .param(FieldBuilder("endTime", FieldType::String)
                .description("End time (YYYY-MM-DD HH:mm:ss)"))
            .param(FieldBuilder("count", FieldType::Int).required())
            .param(FieldBuilder("offset", FieldType::Int).required())
            .param(FieldBuilder("desc", FieldType::Bool).defaultValue(true))
            .returns(FieldType::Array, "Log list"))

        .command(CommandBuilder("vessellog.last")
            .description("Get latest log for a vessel")
            .group("vessellog")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .returns(FieldType::Object, "VesselLogInfo"))

        .command(CommandBuilder("vessellog.lastAll")
            .description("Get latest logs for multiple vessels")
            .group("vessellog")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::String).required()
                .description("Comma-separated vessel IDs (e.g. 1,2,3)"))
            .returns(FieldType::Array, "Log list"))

        // ========== Material Management ==========
        .command(CommandBuilder("material.list")
            .description("Get all materials list")
            .group("material")
            .param(addrParam())
            .returns(FieldType::Array, "Material list"))

        .command(CommandBuilder("material.get")
            .description("Get material details")
            .group("material")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Material info"))

        .command(CommandBuilder("material.add")
            .description("Create or update material")
            .group("material")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .param(FieldBuilder("densityType", FieldType::String)
                .description("LevelDensityTable/VolumeDensityTable"))
            .param(FieldBuilder("densityTable", FieldType::Array))
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("material.del")
            .description("Delete material")
            .group("material")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Empty on success"))

        // ========== Filter Management ==========
        .command(CommandBuilder("filter.list")
            .description("Get all filters list")
            .group("filter")
            .param(addrParam())
            .param(tokenParam())
            .returns(FieldType::Array, "Filter list"))

        .command(CommandBuilder("filter.detail")
            .description("Get filter details")
            .group("filter")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Filter info with content"))

        .command(CommandBuilder("filter.replace")
            .description("Create or update filter")
            .group("filter")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .param(FieldBuilder("predefined", FieldType::Bool).required())
            .param(FieldBuilder("content", FieldType::String).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("filter.del")
            .description("Delete filter")
            .group("filter")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("filter.exists")
            .description("Check if filter exists")
            .group("filter")
            .param(addrParam())
            .param(tokenParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .returns(FieldType::Object, "Contains exists boolean"))

        // ========== Platform Operations ==========
        .command(CommandBuilder("platform.version")
            .description("Get system version")
            .group("platform")
            .param(addrParam())
            .returns(FieldType::Object, "Contains version string"))

        .command(CommandBuilder("platform.console")
            .description("Show or hide console window (Windows only)")
            .group("platform")
            .param(addrParam())
            .param(FieldBuilder("show", FieldType::Bool).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("platform.guideInfo")
            .description("Get system initialization guide info")
            .group("platform")
            .param(addrParam())
            .returns(FieldType::Object, "Serial ports, filters, group names"))

        .command(CommandBuilder("platform.uploadModel")
            .description("Upload 3D model file")
            .group("platform")
            .param(addrParam())
            .param(FieldBuilder("extension", FieldType::String).defaultValue("osg"))
            .param(FieldBuilder("data", FieldType::String).required()
                .description("Base64 encoded model data"))
            .returns(FieldType::Object, "Hash and file URL"))

        .command(CommandBuilder("platform.backupSystem")
            .description("Backup system configuration")
            .group("platform")
            .param(addrParam())
            .returns(FieldType::Object, "Backup file path"))

        .command(CommandBuilder("platform.restoreSystem")
            .description("Restore system from backup")
            .group("platform")
            .param(addrParam())
            .param(FieldBuilder("path", FieldType::String).required())
            .returns(FieldType::Object, "Empty on success"))

        .command(CommandBuilder("platform.settings")
            .description("Get system settings")
            .group("platform")
            .param(addrParam())
            .returns(FieldType::Object, "Settings and version"))

        // ========== Custom Model ==========
        .command(CommandBuilder("custommodel.upload")
            .description("Upload custom 3D model (PLY)")
            .group("custommodel")
            .param(addrParam())
            .param(FieldBuilder("name", FieldType::String).required())
            .param(FieldBuilder("data", FieldType::String).required()
                .description("Base64 encoded PLY data"))
            .returns(FieldType::Object, "Model id, name, hash"))

        .command(CommandBuilder("custommodel.list")
            .description("Get all custom models")
            .group("custommodel")
            .param(addrParam())
            .returns(FieldType::Array, "Model list"))

        .command(CommandBuilder("custommodel.del")
            .description("Delete custom model")
            .group("custommodel")
            .param(addrParam())
            .param(FieldBuilder("id", FieldType::Int).required())
            .returns(FieldType::Object, "Empty on success"))

        // ========== WebSocket ==========
        .command(CommandBuilder("ws.connect")
            .description("Connect to WebSocket for real-time events")
            .group("websocket")
            .param(addrParam())
            .returns(FieldType::Object, "Connection status")
            .event("scanner.ready", "Scanner ready")
            .event("scanner.scanning", "Scanning in progress")
            .event("scanner.progress", "Scan progress")
            .event("scanner.result", "Scan result")
            .event("scanner.error", "Scanner error")
            .event("scanner.event", "Event log")
            .event("scanner.created", "Vessel created")
            .event("scanner.modified", "Vessel modified")
            .event("scanner.deleted", "Vessel deleted")
            .event("ws.disconnected", "WebSocket disconnected")
            .event("ws.error", "WebSocket error"))

        .command(CommandBuilder("ws.subscribe")
            .description("Subscribe to event topic")
            .group("websocket")
            .param(FieldBuilder("topic", FieldType::String)
                .required()
                .defaultValue("vessel.notify"))
            .returns(FieldType::Object, "Subscription status"))

        .command(CommandBuilder("ws.unsubscribe")
            .description("Unsubscribe from event topic")
            .group("websocket")
            .param(FieldBuilder("topic", FieldType::String).required())
            .returns(FieldType::Object, "Unsubscription status"))

        .command(CommandBuilder("ws.disconnect")
            .description("Disconnect WebSocket")
            .group("websocket")
            .returns(FieldType::Object, "Disconnection status"))

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

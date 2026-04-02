#include "driver_opcua_server/handler.h"

#include <QJsonArray>
#include <QJsonObject>

#include "opcua_common.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;
using namespace opcua_common;

namespace {

constexpr int kTransportErrorCode = 1;
constexpr int kOpcUaErrorCode = 2;
constexpr int kInvalidParamCode = 3;

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(kInvalidParamCode, QJsonObject{{"message", message}});
}

void respondTransportError(IResponder& responder, const QString& message) {
    responder.error(kTransportErrorCode, QJsonObject{{"message", message}});
}

void respondOpcUaError(IResponder& responder, const QString& message) {
    responder.error(kOpcUaErrorCode, QJsonObject{{"message", message}});
}

bool expectString(const QJsonObject& object,
                  const QString& key,
                  QString& target,
                  const QString& defaultValue,
                  QString& errorMessage) {
    if (!object.contains(key)) {
        target = defaultValue;
        return true;
    }
    if (!object.value(key).isString()) {
        errorMessage = QString("%1 must be a string").arg(key);
        return false;
    }
    target = object.value(key).toString();
    return true;
}

bool expectBool(const QJsonObject& object,
                const QString& key,
                bool& target,
                bool defaultValue,
                QString& errorMessage) {
    if (!object.contains(key)) {
        target = defaultValue;
        return true;
    }
    if (!object.value(key).isBool()) {
        errorMessage = QString("%1 must be a boolean").arg(key);
        return false;
    }
    target = object.value(key).toBool(defaultValue);
    return true;
}

bool expectInt(const QJsonObject& object,
               const QString& key,
               int& target,
               int defaultValue,
               int minValue,
               int maxValue,
               QString& errorMessage) {
    if (!object.contains(key)) {
        target = defaultValue;
        return true;
    }
    if (!object.value(key).isDouble()) {
        errorMessage = QString("%1 must be an integer").arg(key);
        return false;
    }
    const double raw = object.value(key).toDouble(defaultValue);
    const int parsed = static_cast<int>(raw);
    if (raw != static_cast<double>(parsed) || parsed < minValue || parsed > maxValue) {
        errorMessage = QString("%1 must be in [%2, %3]").arg(key).arg(minValue).arg(maxValue);
        return false;
    }
    target = parsed;
    return true;
}

bool isValidEventMode(const QString& eventMode) {
    return eventMode == "none" || eventMode == "write"
        || eventMode == "session" || eventMode == "all";
}

bool resolveStartOptions(const QJsonObject& params,
                         OpcUaServerRuntime::StartOptions& options,
                         QString& errorMessage) {
    if (!expectString(params, "bind_host", options.bindHost, "0.0.0.0", errorMessage)) {
        return false;
    }
    int port = 4840;
    if (!expectInt(params, "listen_port", port, 4840, 1, 65535, errorMessage)) {
        return false;
    }
    options.listenPort = static_cast<quint16>(port);
    if (!expectString(params, "endpoint_path", options.endpointPath, "", errorMessage)
        || !expectString(params, "server_name", options.serverName, "stdiolink OPC UA Server", errorMessage)
        || !expectString(params, "application_uri", options.applicationUri,
                         "urn:stdiolink:opcua:server", errorMessage)
        || !expectString(params, "namespace_uri", options.namespaceUri,
                         "urn:stdiolink:opcua:nodes", errorMessage)
        || !expectString(params, "event_mode", options.eventMode, "write", errorMessage)) {
        return false;
    }

    options.eventMode = options.eventMode.trimmed().toLower();
    if (!isValidEventMode(options.eventMode)) {
        errorMessage = QString("event_mode must be one of none/write/session/all");
        return false;
    }
    return true;
}

bool resolveNodeArray(const QJsonObject& params,
                      const QString& key,
                      QJsonArray& nodes,
                      QString& errorMessage,
                      bool required) {
    if (!params.contains(key)) {
        if (required) {
            errorMessage = QString("%1 is required").arg(key);
            return false;
        }
        nodes = QJsonArray{};
        return true;
    }
    if (!params.value(key).isArray()) {
        errorMessage = QString("%1 must be an array").arg(key);
        return false;
    }
    nodes = params.value(key).toArray();
    return true;
}

bool resolveNodeIdList(const QJsonObject& params,
                       const QString& key,
                       QStringList& nodeIds,
                       QString& errorMessage) {
    QJsonArray array;
    if (!resolveNodeArray(params, key, array, errorMessage, true)) {
        return false;
    }
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isString()) {
            errorMessage = QString("%1[%2] must be a string").arg(key).arg(i);
            return false;
        }
        nodeIds.append(array.at(i).toString());
    }
    return true;
}

} // namespace

OpcUaServerHandler::OpcUaServerHandler() {
    m_eventResponder = &m_stdioResponder;
    m_runtime.setEventCallback([this](const QString& eventName, const QJsonObject& payload) {
        std::lock_guard<std::recursive_mutex> lock(m_outputMutex);
        if (m_eventResponder) {
            m_eventResponder->event(eventName, 0, payload);
        }
    });
    buildMeta();
}

void OpcUaServerHandler::setEventResponder(IResponder* responder) {
    m_eventResponder = responder ? responder : &m_stdioResponder;
}

void OpcUaServerHandler::handle(const QString& cmd,
                                const QJsonValue& data,
                                IResponder& responder) {
    std::lock_guard<std::recursive_mutex> lock(m_outputMutex);
    const QJsonObject params = data.toObject();

    if (cmd == "status") {
        responder.done(0, QJsonObject{
            {"status", "ready"},
            {"running", m_runtime.isRunning()},
            {"endpoint", m_runtime.endpoint()},
            {"namespace_uri", m_runtime.namespaceUri()},
            {"namespace_index", m_runtime.namespaceIndex()},
            {"node_count", m_runtime.nodeCount()},
            {"event_mode", m_runtime.eventMode()}
        });
        return;
    }

    if (cmd == "start_server") {
        if (m_runtime.isRunning()) {
            respondInvalidParam(responder, "server already running");
            return;
        }
        OpcUaServerRuntime::StartOptions options;
        QString errorMessage;
        if (!resolveStartOptions(params, options, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (!m_runtime.start(options, errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }

        const QJsonObject payload{
            {"started", true},
            {"endpoint", m_runtime.endpoint()},
            {"namespace_uri", m_runtime.namespaceUri()},
            {"namespace_index", m_runtime.namespaceIndex()},
            {"event_mode", m_runtime.eventMode()}
        };
        responder.done(0, payload);
        if (m_eventResponder) {
            m_eventResponder->event("started", 0, QJsonObject{
                {"endpoint", m_runtime.endpoint()},
                {"namespace_uri", m_runtime.namespaceUri()},
                {"namespace_index", m_runtime.namespaceIndex()},
                {"event_mode", m_runtime.eventMode()}
            });
        }
        return;
    }

    if (cmd == "stop_server") {
        if (!m_runtime.isRunning()) {
            respondInvalidParam(responder, "server not running");
            return;
        }
        int gracefulTimeoutMs = 3000;
        QString errorMessage;
        if (!expectInt(params, "graceful_timeout_ms", gracefulTimeoutMs, 3000, 1, 60000, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (!m_runtime.stop(gracefulTimeoutMs, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{{"stopped", true}});
        if (m_eventResponder) {
            m_eventResponder->event("stopped", 0, QJsonObject{});
        }
        return;
    }

    if (!m_runtime.isRunning() && cmd != "run") {
        respondInvalidParam(responder, "server not running");
        return;
    }

    if (cmd == "run") {
        if (m_runtime.isRunning()) {
            respondInvalidParam(responder, "server already running");
            return;
        }

        OpcUaServerRuntime::StartOptions options;
        QString errorMessage;
        if (!resolveStartOptions(params, options, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }

        QJsonArray nodes;
        if (!resolveNodeArray(params, "nodes", nodes, errorMessage, false)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }

        if (!m_runtime.start(options, errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }

        QJsonArray upserted;
        if (!nodes.isEmpty() && !m_runtime.upsertNodes(nodes, upserted, errorMessage)) {
            QString stopError;
            m_runtime.stop(3000, stopError);
            respondOpcUaError(responder, errorMessage);
            return;
        }

        if (m_eventResponder) {
            m_eventResponder->event("started", 0, QJsonObject{
                {"endpoint", m_runtime.endpoint()},
                {"namespace_uri", m_runtime.namespaceUri()},
                {"namespace_index", m_runtime.namespaceIndex()},
                {"event_mode", m_runtime.eventMode()},
                {"node_count", m_runtime.nodeCount()},
                {"upserted", upserted}
            });
        }
        return;
    }

    if (cmd == "upsert_nodes") {
        QJsonArray nodes;
        QString errorMessage;
        if (!resolveNodeArray(params, "nodes", nodes, errorMessage, true)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        QJsonArray results;
        if (!m_runtime.upsertNodes(nodes, results, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{{"results", results}});
        return;
    }

    if (cmd == "delete_nodes") {
        QStringList nodeIds;
        QString errorMessage;
        if (!resolveNodeIdList(params, "node_ids", nodeIds, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        bool recursive = false;
        if (!expectBool(params, "recursive", recursive, false, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        QJsonArray results;
        if (!m_runtime.deleteNodes(nodeIds, recursive, results, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{{"results", results}});
        return;
    }

    if (cmd == "write_values") {
        QJsonArray items;
        QString errorMessage;
        if (!resolveNodeArray(params, "items", items, errorMessage, true)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        bool strictType = true;
        if (!expectBool(params, "strict_type", strictType, true, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        QJsonArray results;
        if (!m_runtime.writeValues(items, strictType, results, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{{"results", results}});
        return;
    }

    if (cmd == "inspect_node") {
        QString nodeId;
        QString errorMessage;
        if (!expectString(params, "node_id", nodeId, "", errorMessage) || nodeId.trimmed().isEmpty()) {
            respondInvalidParam(responder, nodeId.trimmed().isEmpty() ? "node_id is required" : errorMessage);
            return;
        }
        bool recurse = false;
        if (!expectBool(params, "recurse", recurse, false, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        QJsonObject node;
        if (!m_runtime.inspectNode(nodeId, recurse, node, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"endpoint", m_runtime.endpoint()},
            {"node", node}
        });
        return;
    }

    if (cmd == "snapshot_nodes") {
        QString rootNodeId;
        QString errorMessage;
        if (!expectString(params, "root_node_id", rootNodeId, "", errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        bool recurse = true;
        if (!expectBool(params, "recurse", recurse, true, errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        QJsonObject node;
        if (!m_runtime.snapshotNodes(rootNodeId, recurse, node, errorMessage)) {
            respondOpcUaError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"endpoint", m_runtime.endpoint()},
            {"node", node}
        });
        return;
    }

    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

void OpcUaServerHandler::buildMeta() {
    const FieldBuilder nodeField = buildSnapshotNodeField();
    const FieldBuilder nodeItem = FieldBuilder("node", FieldType::Object)
        .addField(FieldBuilder("node_id", FieldType::String)
            .required()
            .description(QString::fromUtf8("节点 NodeId，如 ns=1;s=Plant.Line1.Temp")))
        .addField(FieldBuilder("parent_node_id", FieldType::String)
            .required()
            .description(QString::fromUtf8("父节点 NodeId；顶层节点可使用 i=85")))
        .addField(FieldBuilder("node_class", FieldType::Enum)
            .required()
            .enumValues(QStringList{"folder", "variable"})
            .description(QString::fromUtf8("节点类型，仅支持 folder / variable")))
        .addField(FieldBuilder("browse_name", FieldType::String)
            .required()
            .description(QString::fromUtf8("BrowseName，不含 namespace 前缀")))
        .addField(FieldBuilder("display_name", FieldType::String)
            .description(QString::fromUtf8("显示名称，缺省时回落为 browse_name")))
        .addField(FieldBuilder("description", FieldType::String)
            .description(QString::fromUtf8("节点描述")))
        .addField(FieldBuilder("data_type", FieldType::Enum)
            .enumValues(QStringList{
                "bool", "int16", "uint16", "int32", "uint32", "int64", "uint64",
                "float", "double", "string", "bytestring", "datetime"
            })
            .description(QString::fromUtf8("变量节点数据类型")))
        .addField(FieldBuilder("access", FieldType::Enum)
            .enumValues(QStringList{"read_only", "read_write"})
            .description(QString::fromUtf8("变量节点访问模式")))
        .addField(FieldBuilder("initial_value", FieldType::Any)
            .description(QString::fromUtf8("变量节点初始值；int64/uint64 使用十进制字符串，bytestring 使用 base64，datetime 使用 ISO8601")));

    auto startCommand = CommandBuilder("start_server")
        .description(QString::fromUtf8("启动本地 OPC UA 服务端并开始监听"))
        .param(FieldBuilder("bind_host", FieldType::String)
            .defaultValue("0.0.0.0")
            .description(QString::fromUtf8("监听地址，默认 0.0.0.0")))
        .param(FieldBuilder("listen_port", FieldType::Int)
            .defaultValue(4840)
            .range(1, 65535)
            .description(QString::fromUtf8("监听端口，默认 4840")))
        .param(FieldBuilder("endpoint_path", FieldType::String)
            .defaultValue("")
            .description(QString::fromUtf8("Endpoint URL 的路径部分，默认空")))
        .param(FieldBuilder("server_name", FieldType::String)
            .defaultValue("stdiolink OPC UA Server")
            .description(QString::fromUtf8("服务端显示名称")))
        .param(FieldBuilder("application_uri", FieldType::String)
            .defaultValue("urn:stdiolink:opcua:server")
            .description(QString::fromUtf8("ApplicationUri")))
        .param(FieldBuilder("namespace_uri", FieldType::String)
            .defaultValue("urn:stdiolink:opcua:nodes")
            .description(QString::fromUtf8("业务命名空间 URI，固定映射到 ns=1")))
        .param(FieldBuilder("event_mode", FieldType::Enum)
            .defaultValue("write")
            .enumValues(QStringList{"none", "write", "session", "all"})
            .description(QString::fromUtf8("事件推送模式：none=无 / write=仅写入 / session=仅会话 / all=全部")))
        .event("started", QString::fromUtf8("服务端启动事件"))
        .event("session_activated", QString::fromUtf8("外部客户端 Session 激活事件"))
        .event("session_closed", QString::fromUtf8("外部客户端 Session 关闭事件"))
        .event("node_value_changed", QString::fromUtf8("节点值变更事件"))
        .example(QString::fromUtf8("启动 OPC UA 服务端"), QStringList{"stdio", "console"},
                 QJsonObject{{"bind_host", "127.0.0.1"}, {"listen_port", 4840}});

    auto runCommand = CommandBuilder("run")
        .description(QString::fromUtf8("一键启动服务端、装载节点并进入 keepalive 事件流；成功后仅发送 started 事件，不返回 done"))
        .param(FieldBuilder("bind_host", FieldType::String).defaultValue("0.0.0.0"))
        .param(FieldBuilder("listen_port", FieldType::Int).defaultValue(4840).range(1, 65535))
        .param(FieldBuilder("endpoint_path", FieldType::String).defaultValue(""))
        .param(FieldBuilder("server_name", FieldType::String).defaultValue("stdiolink OPC UA Server"))
        .param(FieldBuilder("application_uri", FieldType::String).defaultValue("urn:stdiolink:opcua:server"))
        .param(FieldBuilder("namespace_uri", FieldType::String).defaultValue("urn:stdiolink:opcua:nodes"))
        .param(FieldBuilder("event_mode", FieldType::Enum)
            .defaultValue("write")
            .enumValues(QStringList{"none", "write", "session", "all"}))
        .param(FieldBuilder("nodes", FieldType::Array)
            .defaultValue(QJsonArray{})
            .description(QString::fromUtf8("启动后需要装载的节点定义数组"))
            .items(nodeItem))
        .event("started", QString::fromUtf8("服务端启动事件"))
        .event("node_value_changed", QString::fromUtf8("节点值变更事件"))
        .example(QString::fromUtf8("启动服务端并装载最小节点"), QStringList{"stdio", "console"},
                 QJsonObject{
                     {"bind_host", "127.0.0.1"},
                     {"listen_port", 4840},
                     {"nodes", QJsonArray{
                         QJsonObject{
                             {"node_id", "ns=1;s=Plant"},
                             {"parent_node_id", "i=85"},
                             {"node_class", "folder"},
                             {"browse_name", "Plant"},
                             {"display_name", "Plant"}
                         }
                     }}
                 });

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.opcua_server",
              "OPC UA Server",
              "1.0.0",
              QString::fromUtf8("基于 open62541 的 KeepAlive OPC UA 服务端驱动，支持动态建点、删点、写值和业务树快照"))
        .vendor("stdiolink")
        .profile("keepalive")
        .command(CommandBuilder("status")
            .description(QString::fromUtf8("查询驱动与服务端状态"))
            .example(QString::fromUtf8("查询驱动状态"), QStringList{"stdio", "console"}, QJsonObject{}))
        .command(runCommand)
        .command(startCommand)
        .command(CommandBuilder("stop_server")
            .description(QString::fromUtf8("停止 OPC UA 服务端"))
            .param(FieldBuilder("graceful_timeout_ms", FieldType::Int)
                .defaultValue(3000)
                .range(1, 60000)
                .description(QString::fromUtf8("停止等待时间(ms)")))
            .event("stopped", QString::fromUtf8("服务端停止事件"))
            .example(QString::fromUtf8("停止服务端"), QStringList{"stdio", "console"}, QJsonObject{}))
        .command(CommandBuilder("upsert_nodes")
            .description(QString::fromUtf8("批量创建或更新 folder / variable 节点"))
            .param(FieldBuilder("nodes", FieldType::Array)
                .required()
                .description(QString::fromUtf8("节点定义数组，允许父子节点无序"))
                .items(nodeItem))
            .example(QString::fromUtf8("创建样例变量节点"), QStringList{"stdio", "console"},
                     QJsonObject{
                         {"nodes", QJsonArray{
                             QJsonObject{
                                 {"node_id", "ns=1;s=Plant.Line1.Temp"},
                                 {"parent_node_id", "ns=1;s=Plant.Line1"},
                                 {"node_class", "variable"},
                                 {"browse_name", "Temp"},
                                 {"display_name", "Temp"},
                                 {"description", "温度"},
                                 {"data_type", "double"},
                                 {"access", "read_only"},
                                 {"initial_value", 36.5}
                             }
                         }}
                     }))
        .command(CommandBuilder("delete_nodes")
            .description(QString::fromUtf8("批量删除节点；目录节点需显式 recursive=true"))
            .param(FieldBuilder("node_ids", FieldType::Array)
                .required()
                .items(FieldBuilder("node_id", FieldType::String)))
            .param(FieldBuilder("recursive", FieldType::Bool)
                .defaultValue(false)
                .description(QString::fromUtf8("是否递归删除目录节点")))
            .example(QString::fromUtf8("删除单个变量节点"), QStringList{"stdio", "console"},
                     QJsonObject{{"node_ids", QJsonArray{"ns=1;s=Plant.Line1.Temp"}}}))
        .command(CommandBuilder("write_values")
            .description(QString::fromUtf8("批量更新变量节点值；read_only 节点会被拒绝"))
            .param(FieldBuilder("strict_type", FieldType::Bool)
                .defaultValue(true)
                .description(QString::fromUtf8("是否严格按目标数据类型校验 JSON 输入")))
            .param(FieldBuilder("items", FieldType::Array)
                .required()
                .items(FieldBuilder("item", FieldType::Object)
                    .addField(FieldBuilder("node_id", FieldType::String).required())
                    .addField(FieldBuilder("value", FieldType::Any).required())
                    .addField(FieldBuilder("source_timestamp", FieldType::String))))
            .event("node_value_changed", QString::fromUtf8("节点值变更事件"))
            .example(QString::fromUtf8("写入可写设定值"), QStringList{"stdio", "console"},
                     QJsonObject{
                         {"items", QJsonArray{
                             QJsonObject{
                                 {"node_id", "ns=1;s=Plant.Line1.SetPoint"},
                                 {"value", 40.0}
                             }
                         }}
                     }))
        .command(CommandBuilder("inspect_node")
            .description(QString::fromUtf8("按 NodeId 查询单个节点；目录节点可按 recurse 递归子树"))
            .param(FieldBuilder("node_id", FieldType::String).required())
            .param(FieldBuilder("recurse", FieldType::Bool).defaultValue(false))
            .returnField(FieldBuilder("result", FieldType::Object)
                .addField(FieldBuilder("endpoint", FieldType::String))
                .addField(nodeField))
            .example(QString::fromUtf8("查询单个节点"), QStringList{"stdio", "console"},
                     QJsonObject{{"node_id", "ns=1;s=Plant.Line1.Temp"}}))
        .command(CommandBuilder("snapshot_nodes")
            .description(QString::fromUtf8("导出业务树快照；默认根节点为 ObjectsFolder(i=85)"))
            .param(FieldBuilder("root_node_id", FieldType::String)
                .defaultValue("")
                .description(QString::fromUtf8("快照根节点；空字符串表示 i=85")))
            .param(FieldBuilder("recurse", FieldType::Bool)
                .defaultValue(true)
                .description(QString::fromUtf8("是否递归遍历全部后代节点")))
            .returnField(FieldBuilder("result", FieldType::Object)
                .addField(FieldBuilder("endpoint", FieldType::String))
                .addField(nodeField))
            .example(QString::fromUtf8("导出完整业务树"), QStringList{"stdio", "console"}, QJsonObject{}))
        .build();
}

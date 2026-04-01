#include "driver_opcua/handler.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

extern "C" {
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
}

#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

namespace {

constexpr int kInvalidParamCode = 3;
constexpr int kTransportErrorCode = 1;
constexpr int kOpcUaErrorCode = 2;

void silentOpen62541Log(void* logContext,
                        UA_LogLevel level,
                        UA_LogCategory category,
                        const char* msg,
                        va_list args) {
    (void)logContext;
    (void)level;
    (void)category;
    (void)msg;
    (void)args;
}

UA_Logger* silentOpen62541Logger() {
    static UA_Logger logger{silentOpen62541Log, nullptr, nullptr};
    return &logger;
}

QString uaStringToQString(const UA_String& value) {
    if (!value.data || value.length == 0) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data),
                             static_cast<int>(value.length));
}

QString localizedTextToQString(const UA_LocalizedText& value) {
    return uaStringToQString(value.text);
}

QString qualifiedNameToQString(const UA_QualifiedName& value) {
    const QString name = uaStringToQString(value.name);
    return QString("%1:%2").arg(value.namespaceIndex).arg(name);
}

QString guidToQString(const UA_Guid& value) {
    const QUuid guid(value.data1,
                     value.data2,
                     value.data3,
                     value.data4[0],
                     value.data4[1],
                     value.data4[2],
                     value.data4[3],
                     value.data4[4],
                     value.data4[5],
                     value.data4[6],
                     value.data4[7]);
    return guid.toString(QUuid::WithoutBraces);
}

QString statusCodeText(UA_StatusCode statusCode) {
    const char* name = UA_StatusCode_name(statusCode);
    if (!name) {
        return QString("0x%1").arg(QString::number(statusCode, 16));
    }
    return QString::fromUtf8(name);
}

QString formatStatusMessage(const QString& action, UA_StatusCode statusCode) {
    return QString("%1: %2").arg(action, statusCodeText(statusCode));
}

QString nodeIdToQString(const UA_NodeId& nodeId) {
    UA_String printed = UA_STRING_NULL;
    const UA_StatusCode statusCode = UA_NodeId_print(&nodeId, &printed);
    if (statusCode != UA_STATUSCODE_GOOD) {
        return formatStatusMessage("print NodeId failed", statusCode);
    }
    const QString text = uaStringToQString(printed);
    UA_String_clear(&printed);
    return text;
}

QString expandedNodeIdToQString(const UA_ExpandedNodeId& nodeId) {
    if (UA_ExpandedNodeId_isLocal(&nodeId)) {
        return nodeIdToQString(nodeId.nodeId);
    }
    UA_String printed = UA_STRING_NULL;
    const UA_StatusCode statusCode = UA_ExpandedNodeId_print(&nodeId, &printed);
    if (statusCode != UA_STATUSCODE_GOOD) {
        return formatStatusMessage("print ExpandedNodeId failed", statusCode);
    }
    const QString text = uaStringToQString(printed);
    UA_String_clear(&printed);
    return text;
}

QString nodeClassToQString(UA_NodeClass nodeClass) {
    switch (nodeClass) {
    case UA_NODECLASS_UNSPECIFIED: return "unspecified";
    case UA_NODECLASS_OBJECT: return "object";
    case UA_NODECLASS_VARIABLE: return "variable";
    case UA_NODECLASS_METHOD: return "method";
    case UA_NODECLASS_OBJECTTYPE: return "object_type";
    case UA_NODECLASS_VARIABLETYPE: return "variable_type";
    case UA_NODECLASS_REFERENCETYPE: return "reference_type";
    case UA_NODECLASS_DATATYPE: return "data_type";
    case UA_NODECLASS_VIEW: return "view";
    default: return "unknown";
    }
}

bool isDirectoryNodeClass(UA_NodeClass nodeClass) {
    return nodeClass == UA_NODECLASS_OBJECT || nodeClass == UA_NODECLASS_VIEW;
}

bool isBrowsableSnapshotNode(UA_NodeClass nodeClass) {
    return isDirectoryNodeClass(nodeClass) || nodeClass == UA_NODECLASS_VARIABLE;
}

QString jsonValueToText(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return {};
    case QJsonValue::Bool:
        return value.toBool() ? "true" : "false";
    case QJsonValue::Double:
        return QString::number(value.toDouble(), 'g', 16);
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QJsonValue scalarToJson(const void* data, const UA_DataType* type) {
    if (!data || !type) {
        return QJsonValue();
    }

    if (type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        return static_cast<bool>(*static_cast<const UA_Boolean*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_SBYTE]) {
        return static_cast<int>(*static_cast<const UA_SByte*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_BYTE]) {
        return static_cast<int>(*static_cast<const UA_Byte*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_INT16]) {
        return static_cast<int>(*static_cast<const UA_Int16*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_UINT16]) {
        return static_cast<int>(*static_cast<const UA_UInt16*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_INT32]) {
        return static_cast<qint64>(*static_cast<const UA_Int32*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_UINT32]) {
        return static_cast<qint64>(*static_cast<const UA_UInt32*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_INT64]) {
        return QString::number(*static_cast<const UA_Int64*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_UINT64]) {
        return QString::number(*static_cast<const UA_UInt64*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_FLOAT]) {
        return static_cast<double>(*static_cast<const UA_Float*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_DOUBLE]) {
        return *static_cast<const UA_Double*>(data);
    }
    if (type == &UA_TYPES[UA_TYPES_STRING]) {
        return uaStringToQString(*static_cast<const UA_String*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_BYTESTRING]) {
        const auto& value = *static_cast<const UA_ByteString*>(data);
        const QByteArray bytes(reinterpret_cast<const char*>(value.data),
                               static_cast<int>(value.length));
        return QString::fromUtf8(bytes.toBase64());
    }
    if (type == &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) {
        return localizedTextToQString(*static_cast<const UA_LocalizedText*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) {
        return qualifiedNameToQString(*static_cast<const UA_QualifiedName*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_NODEID]) {
        return nodeIdToQString(*static_cast<const UA_NodeId*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_EXPANDEDNODEID]) {
        return expandedNodeIdToQString(*static_cast<const UA_ExpandedNodeId*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_GUID]) {
        return guidToQString(*static_cast<const UA_Guid*>(data));
    }
    if (type == &UA_TYPES[UA_TYPES_DATETIME]) {
        return QString::number(*static_cast<const UA_DateTime*>(data));
    }

    return QString("<unsupported:%1>").arg(QString::fromUtf8(type->typeName));
}

QJsonValue variantToJson(const UA_Variant& variant) {
    if (UA_Variant_isEmpty(&variant) || !variant.type) {
        return QJsonValue();
    }

    if (UA_Variant_isScalar(&variant)) {
        return scalarToJson(variant.data, variant.type);
    }

    QJsonArray array;
    const char* base = static_cast<const char*>(variant.data);
    for (size_t i = 0; i < variant.arrayLength; ++i) {
        const void* item = base + i * variant.type->memSize;
        array.append(scalarToJson(item, variant.type));
    }
    return array;
}

FieldBuilder connectionParam(const QString& name) {
    if (name == "host") {
        return FieldBuilder("host", FieldType::String)
            .required()
            .description(QString::fromUtf8("OPC UA 服务器地址，如 127.0.0.1"))
            .placeholder("127.0.0.1");
    }
    if (name == "port") {
        return FieldBuilder("port", FieldType::Int)
            .defaultValue(4840)
            .range(1, 65535)
            .description(QString::fromUtf8("OPC UA 服务器端口，默认 4840"));
    }
    return FieldBuilder("timeout_ms", FieldType::Int)
        .defaultValue(3000)
        .range(1, 30000)
        .unit("ms")
        .description(QString::fromUtf8("单次连接/读取超时（毫秒），默认 3000"));
}

void addConnectionParams(CommandBuilder& command) {
    command.param(connectionParam("host"))
        .param(connectionParam("port"))
        .param(connectionParam("timeout_ms"));
}

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(kInvalidParamCode, QJsonObject{{"message", message}});
}

void respondTransportError(IResponder& responder, const QString& message) {
    responder.error(kTransportErrorCode, QJsonObject{{"message", message}});
}

void respondOpcUaError(IResponder& responder, const QString& message) {
    responder.error(kOpcUaErrorCode, QJsonObject{{"message", message}});
}

class UaClientHandle {
public:
    ~UaClientHandle();

    bool connect(const OpcUaConnectionOptions& options, QString& errorMessage);
    UA_Client* client() const { return m_client; }
    QString endpoint() const { return m_endpoint; }

private:
    UA_Client* m_client = nullptr;
    QString m_endpoint;
};

UaClientHandle::~UaClientHandle() {
    if (!m_client) {
        return;
    }
    UA_Client_disconnect(m_client);
    UA_Client_delete(m_client);
}

bool UaClientHandle::connect(const OpcUaConnectionOptions& options, QString& errorMessage) {
    m_endpoint = QString("opc.tcp://%1:%2").arg(options.host).arg(options.port);
    UA_ClientConfig config{};
    config.logging = silentOpen62541Logger();

    UA_StatusCode statusCode = UA_ClientConfig_setDefault(&config);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("UA_ClientConfig_setDefault failed", statusCode);
        UA_ClientConfig_clear(&config);
        return false;
    }
    config.timeout = static_cast<UA_UInt32>(options.timeoutMs);

    m_client = UA_Client_newWithConfig(&config);
    if (!m_client) {
        errorMessage = "Failed to allocate OPC UA client";
        UA_ClientConfig_clear(&config);
        return false;
    }

    const QByteArray endpointUtf8 = m_endpoint.toUtf8();
    statusCode = UA_Client_connect(m_client, endpointUtf8.constData());
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("UA_Client_connect failed", statusCode);
        return false;
    }
    return true;
}

bool readNodeClass(UA_Client* client,
                   const UA_NodeId& nodeId,
                   UA_NodeClass& nodeClass,
                   QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Client_readNodeClassAttribute(client, nodeId, &nodeClass);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read node class failed", statusCode);
        return false;
    }
    return true;
}

bool readBrowseName(UA_Client* client,
                    const UA_NodeId& nodeId,
                    UA_QualifiedName& browseName,
                    QString& errorMessage) {
    UA_QualifiedName_init(&browseName);
    const UA_StatusCode statusCode = UA_Client_readBrowseNameAttribute(client, nodeId, &browseName);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read browse name failed", statusCode);
        return false;
    }
    return true;
}

bool readDisplayName(UA_Client* client,
                     const UA_NodeId& nodeId,
                     UA_LocalizedText& displayName,
                     QString& errorMessage) {
    UA_LocalizedText_init(&displayName);
    const UA_StatusCode statusCode = UA_Client_readDisplayNameAttribute(client, nodeId, &displayName);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read display name failed", statusCode);
        return false;
    }
    return true;
}

bool readDescription(UA_Client* client,
                     const UA_NodeId& nodeId,
                     UA_LocalizedText& description,
                     QString& errorMessage) {
    UA_LocalizedText_init(&description);
    const UA_StatusCode statusCode = UA_Client_readDescriptionAttribute(client, nodeId, &description);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read description failed", statusCode);
        return false;
    }
    return true;
}

QString readDataTypeName(UA_Client* client, const UA_NodeId& dataTypeId) {
    UA_QualifiedName browseName;
    UA_QualifiedName_init(&browseName);
    const UA_StatusCode statusCode =
        UA_Client_readBrowseNameAttribute(client, dataTypeId, &browseName);
    if (statusCode == UA_STATUSCODE_GOOD) {
        const QString name = uaStringToQString(browseName.name);
        if (!name.isEmpty()) {
            UA_QualifiedName_clear(&browseName);
            return name;
        }
        const QString qualifiedName = qualifiedNameToQString(browseName);
        UA_QualifiedName_clear(&browseName);
        return qualifiedName;
    }
    return nodeIdToQString(dataTypeId);
}

bool parseNodeIdString(const QString& input,
                       UA_NodeId& nodeId,
                       QString& normalizedNodeId,
                       QString* errorMessage) {
    UA_NodeId_init(&nodeId);
    const QByteArray inputUtf8 = input.trimmed().toUtf8();
    if (inputUtf8.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "node_id is required";
        }
        return false;
    }

    UA_String nodeIdText;
    nodeIdText.length = static_cast<size_t>(inputUtf8.size());
    nodeIdText.data = reinterpret_cast<UA_Byte*>(const_cast<char*>(inputUtf8.constData()));
    const UA_StatusCode statusCode = UA_NodeId_parse(&nodeId, nodeIdText);
    if (statusCode != UA_STATUSCODE_GOOD) {
        if (errorMessage) {
            *errorMessage = formatStatusMessage("Invalid node_id", statusCode);
        }
        return false;
    }

    normalizedNodeId = nodeIdToQString(nodeId);
    return true;
}

bool browseChildren(UA_Client* client,
                    const UA_NodeId& nodeId,
                    bool recurseChildren,
                    bool excludeNamespaceZeroChildren,
                    QSet<QString>& visited,
                    QJsonArray& outChildren,
                    QString& errorMessage);

bool readNodeSnapshot(UA_Client* client,
                      const UA_NodeId& nodeId,
                      bool expandChildren,
                      bool recurseChildren,
                      bool excludeNamespaceZeroChildren,
                      QSet<QString>& visited,
                      QJsonObject& outNode,
                      QString& errorMessage);

bool readNodeSnapshot(UA_Client* client,
                      const UA_NodeId& nodeId,
                      bool expandChildren,
                      bool recurseChildren,
                      bool excludeNamespaceZeroChildren,
                      QSet<QString>& visited,
                      QJsonObject& outNode,
                      QString& errorMessage) {
    UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
    if (!readNodeClass(client, nodeId, nodeClass, errorMessage)) {
        return false;
    }

    UA_QualifiedName browseName;
    if (!readBrowseName(client, nodeId, browseName, errorMessage)) {
        return false;
    }
    UA_LocalizedText displayName;
    if (!readDisplayName(client, nodeId, displayName, errorMessage)) {
        UA_QualifiedName_clear(&browseName);
        return false;
    }
    UA_LocalizedText description;
    if (!readDescription(client, nodeId, description, errorMessage)) {
        UA_QualifiedName_clear(&browseName);
        UA_LocalizedText_clear(&displayName);
        return false;
    }

    const QString normalizedNodeId = nodeIdToQString(nodeId);
    const bool alreadyVisited = visited.contains(normalizedNodeId);
    if (!alreadyVisited) {
        visited.insert(normalizedNodeId);
    }

    QJsonObject node{
        {"node_id", normalizedNodeId},
        {"node_class", nodeClassToQString(nodeClass)},
        {"browse_name", qualifiedNameToQString(browseName)},
        {"display_name", localizedTextToQString(displayName)},
        {"description", localizedTextToQString(description)},
        {"is_directory", isDirectoryNodeClass(nodeClass)},
        {"children", QJsonArray{}}
    };

    if (nodeClass == UA_NODECLASS_VARIABLE) {
        UA_NodeId dataTypeId;
        UA_NodeId_init(&dataTypeId);
        UA_Int32 valueRank = -2;
        UA_Byte accessLevel = 0;
        UA_Variant value;
        UA_Variant_init(&value);

        UA_StatusCode statusCode = UA_Client_readDataTypeAttribute(client, nodeId, &dataTypeId);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Read data type failed", statusCode);
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }
        statusCode = UA_Client_readValueRankAttribute(client, nodeId, &valueRank);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Read value rank failed", statusCode);
            UA_NodeId_clear(&dataTypeId);
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }
        statusCode = UA_Client_readAccessLevelAttribute(client, nodeId, &accessLevel);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Read access level failed", statusCode);
            UA_NodeId_clear(&dataTypeId);
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }
        statusCode = UA_Client_readValueAttribute(client, nodeId, &value);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Read value failed", statusCode);
            UA_NodeId_clear(&dataTypeId);
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }

        const QJsonValue valueJson = variantToJson(value);
        node.insert("data_type_id", nodeIdToQString(dataTypeId));
        node.insert("data_type_name", readDataTypeName(client, dataTypeId));
        node.insert("value_rank", valueRank);
        node.insert("access_level", static_cast<int>(accessLevel));
        node.insert("value", valueJson);
        node.insert("value_text", jsonValueToText(valueJson));

        UA_Variant_clear(&value);
        UA_NodeId_clear(&dataTypeId);
    }

    if (isDirectoryNodeClass(nodeClass) && expandChildren && !alreadyVisited) {
        QJsonArray children;
        if (!browseChildren(client,
                            nodeId,
                            recurseChildren,
                            excludeNamespaceZeroChildren,
                            visited,
                            children,
                            errorMessage)) {
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }
        node["children"] = children;
    }

    UA_QualifiedName_clear(&browseName);
    UA_LocalizedText_clear(&displayName);
    UA_LocalizedText_clear(&description);
    outNode = node;
    return true;
}

bool browseChildren(UA_Client* client,
                    const UA_NodeId& nodeId,
                    bool recurseChildren,
                    bool excludeNamespaceZeroChildren,
                    QSet<QString>& visited,
                    QJsonArray& outChildren,
                    QString& errorMessage) {
    UA_BrowseDescription description;
    UA_BrowseDescription_init(&description);
    description.nodeId = nodeId;
    description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    description.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    description.includeSubtypes = true;
    description.resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseRequest request;
    UA_BrowseRequest_init(&request);
    request.nodesToBrowse = &description;
    request.nodesToBrowseSize = 1;
    request.requestedMaxReferencesPerNode = 0;

    UA_BrowseResponse response = UA_Client_Service_browse(client, request);
    if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Browse failed",
                                           response.responseHeader.serviceResult);
        UA_BrowseResponse_clear(&response);
        return false;
    }
    if (response.resultsSize != 1) {
        errorMessage = "Browse failed: unexpected result count";
        UA_BrowseResponse_clear(&response);
        return false;
    }

    auto appendReferences = [&](const UA_BrowseResult& result) -> bool {
        if (result.statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Browse result failed", result.statusCode);
            return false;
        }

        for (size_t i = 0; i < result.referencesSize; ++i) {
            const UA_ReferenceDescription& reference = result.references[i];
            if (!UA_ExpandedNodeId_isLocal(&reference.nodeId)) {
                continue;
            }
            if (!isBrowsableSnapshotNode(reference.nodeClass)) {
                continue;
            }
            if (excludeNamespaceZeroChildren && reference.nodeId.nodeId.namespaceIndex == 0) {
                continue;
            }

            UA_NodeId childNodeId;
            UA_NodeId_init(&childNodeId);
            const UA_StatusCode copyStatus =
                UA_NodeId_copy(&reference.nodeId.nodeId, &childNodeId);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("Copy child NodeId failed", copyStatus);
                return false;
            }

            QJsonObject childNode;
            if (!readNodeSnapshot(client,
                                  childNodeId,
                                  recurseChildren,
                                  recurseChildren,
                                  false,
                                  visited,
                                  childNode,
                                  errorMessage)) {
                UA_NodeId_clear(&childNodeId);
                return false;
            }
            UA_NodeId_clear(&childNodeId);
            outChildren.append(childNode);
        }
        return true;
    };

    if (!appendReferences(response.results[0])) {
        UA_BrowseResponse_clear(&response);
        return false;
    }

    UA_ByteString continuationPoint = UA_BYTESTRING_NULL;
    if (response.results[0].continuationPoint.length > 0) {
        const UA_StatusCode copyStatus =
            UA_ByteString_copy(&response.results[0].continuationPoint, &continuationPoint);
        if (copyStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
            UA_BrowseResponse_clear(&response);
            return false;
        }
    }
    UA_BrowseResponse_clear(&response);

    while (continuationPoint.length > 0) {
        UA_BrowseNextRequest nextRequest;
        UA_BrowseNextRequest_init(&nextRequest);
        nextRequest.releaseContinuationPoints = false;
        nextRequest.continuationPoints = &continuationPoint;
        nextRequest.continuationPointsSize = 1;
        UA_BrowseNextResponse nextResponse =
            UA_Client_Service_browseNext(client, nextRequest);
        UA_ByteString_clear(&continuationPoint);
        if (nextResponse.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("BrowseNext failed",
                                               nextResponse.responseHeader.serviceResult);
            UA_BrowseNextResponse_clear(&nextResponse);
            return false;
        }
        if (nextResponse.resultsSize != 1) {
            errorMessage = "BrowseNext failed: unexpected result count";
            UA_BrowseNextResponse_clear(&nextResponse);
            return false;
        }
        if (!appendReferences(nextResponse.results[0])) {
            UA_BrowseNextResponse_clear(&nextResponse);
            return false;
        }
        if (nextResponse.results[0].continuationPoint.length > 0) {
            const UA_StatusCode copyStatus =
                UA_ByteString_copy(&nextResponse.results[0].continuationPoint, &continuationPoint);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
                UA_BrowseNextResponse_clear(&nextResponse);
                return false;
            }
        }
        UA_BrowseNextResponse_clear(&nextResponse);
    }

    return true;
}

} // namespace

OpcUaHandler::OpcUaHandler() {
    buildMeta();
}

bool OpcUaHandler::resolveConnectionOptions(const QJsonObject& params,
                                            OpcUaConnectionOptions& options,
                                            QString* errorMessage) {
    options.host = params.value("host").toString().trimmed();
    if (options.host.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "host is required";
        }
        return false;
    }

    const int port = params.value("port").toInt(4840);
    if (port < 1 || port > 65535) {
        if (errorMessage) {
            *errorMessage = "port must be between 1 and 65535";
        }
        return false;
    }
    options.port = static_cast<quint16>(port);

    options.timeoutMs = params.value("timeout_ms").toInt(3000);
    if (options.timeoutMs < 1 || options.timeoutMs > 30000) {
        if (errorMessage) {
            *errorMessage = "timeout_ms must be 1-30000";
        }
        return false;
    }

    return true;
}

bool OpcUaHandler::normalizeNodeId(const QString& input,
                                   QString& normalizedNodeId,
                                   QString* errorMessage) {
    UA_NodeId nodeId;
    const bool ok = parseNodeIdString(input, nodeId, normalizedNodeId, errorMessage);
    if (ok) {
        UA_NodeId_clear(&nodeId);
    }
    return ok;
}

void OpcUaHandler::handle(const QString& cmd,
                          const QJsonValue& data,
                          IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    if (cmd != "inspect_node" && cmd != "snapshot_nodes") {
        responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }

    const QJsonObject params = data.toObject();
    OpcUaConnectionOptions options;
    QString errorMessage;
    if (!resolveConnectionOptions(params, options, &errorMessage)) {
        respondInvalidParam(responder, errorMessage);
        return;
    }

    UA_NodeId targetNodeId;
    UA_NodeId_init(&targetNodeId);
    bool expandChildren = true;
    bool recurseChildren = true;
    bool excludeNamespaceZeroChildren = false;

    if (cmd == "inspect_node") {
        const QString nodeIdInput = params.value("node_id").toString();
        QString normalizedNodeId;
        if (!parseNodeIdString(nodeIdInput, targetNodeId, normalizedNodeId, &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        recurseChildren = params.value("recurse").toBool(false);
    } else {
        targetNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        excludeNamespaceZeroChildren = true;
    }

    UaClientHandle clientHandle;
    if (!clientHandle.connect(options, errorMessage)) {
        respondTransportError(responder, errorMessage);
        if (cmd == "inspect_node") {
            UA_NodeId_clear(&targetNodeId);
        }
        return;
    }

    QSet<QString> visited;
    QJsonObject nodeObject;
    if (!readNodeSnapshot(clientHandle.client(),
                          targetNodeId,
                          expandChildren,
                          recurseChildren,
                          excludeNamespaceZeroChildren,
                          visited,
                          nodeObject,
                          errorMessage)) {
        respondOpcUaError(responder, errorMessage);
        if (cmd == "inspect_node") {
            UA_NodeId_clear(&targetNodeId);
        }
        return;
    }

    if (cmd == "inspect_node") {
        UA_NodeId_clear(&targetNodeId);
    }

    responder.done(0, QJsonObject{
        {"endpoint", clientHandle.endpoint()},
        {"node", nodeObject}
    });
}

void OpcUaHandler::buildMeta() {
    FieldBuilder nodeField("node", FieldType::Object);
    nodeField
        .description(QString::fromUtf8("OPC UA 节点快照"))
        .addField(FieldBuilder("node_id", FieldType::String)
            .description(QString::fromUtf8("规范化后的 NodeId 字符串")))
        .addField(FieldBuilder("node_class", FieldType::String)
            .description(QString::fromUtf8("节点类别，如 object / variable")))
        .addField(FieldBuilder("browse_name", FieldType::String)
            .description(QString::fromUtf8("BrowseName，格式 namespaceIndex:name")))
        .addField(FieldBuilder("display_name", FieldType::String)
            .description(QString::fromUtf8("显示名称")))
        .addField(FieldBuilder("description", FieldType::String)
            .description(QString::fromUtf8("节点描述")))
        .addField(FieldBuilder("is_directory", FieldType::Bool)
            .description(QString::fromUtf8("是否为目录节点（Object/View）")))
        .addField(FieldBuilder("data_type_id", FieldType::String)
            .description(QString::fromUtf8("变量节点的数据类型 NodeId")))
        .addField(FieldBuilder("data_type_name", FieldType::String)
            .description(QString::fromUtf8("变量节点的数据类型名称")))
        .addField(FieldBuilder("value_rank", FieldType::Int)
            .description(QString::fromUtf8("变量节点值维度，-1 表示标量")))
        .addField(FieldBuilder("access_level", FieldType::Int)
            .description(QString::fromUtf8("变量节点访问级别掩码")))
        .addField(FieldBuilder("value", FieldType::Any)
            .description(QString::fromUtf8("变量节点值，按常见内置类型序列化为 JSON")))
        .addField(FieldBuilder("value_text", FieldType::String)
            .description(QString::fromUtf8("变量节点值的文本表示")))
        .addField(FieldBuilder("children", FieldType::Array)
            .description(QString::fromUtf8("子节点数组；目录节点返回目录和值节点，值节点为空数组"))
            .items(FieldBuilder("child", FieldType::Any)));

    CommandBuilder inspectNode("inspect_node");
    addConnectionParams(inspectNode);
    inspectNode
        .description(QString::fromUtf8("查询单个 OPC UA 节点；若节点为目录，则返回子目录和值节点，"
                                       "recurse=true 时递归整个子树"))
        .param(FieldBuilder("node_id", FieldType::String)
            .required()
            .description(QString::fromUtf8("原生 NodeId 字符串，如 ns=1;s=Plant.Line1.Temp")))
        .param(FieldBuilder("recurse", FieldType::Bool)
            .defaultValue(false)
            .description(QString::fromUtf8("目录节点是否递归遍历所有后代节点，默认 false")))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description(QString::fromUtf8("查询结果"))
            .addField(FieldBuilder("endpoint", FieldType::String)
                .description(QString::fromUtf8("实际连接到的 OPC UA Endpoint")))
            .addField(nodeField))
        .example(QString::fromUtf8("查询单个温度变量节点"),
                 QStringList{"stdio", "console"},
                 QJsonObject{
                     {"host", "127.0.0.1"},
                     {"port", 4840},
                     {"node_id", "ns=1;s=Plant.Line1.Temp"}
                 });

    CommandBuilder snapshotNodes("snapshot_nodes");
    addConnectionParams(snapshotNodes);
    snapshotNodes
        .description(QString::fromUtf8("从标准 ObjectsFolder 开始递归抓取完整业务树快照，"
                                       "默认过滤标准系统节点"))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description(QString::fromUtf8("快照结果"))
            .addField(FieldBuilder("endpoint", FieldType::String)
                .description(QString::fromUtf8("实际连接到的 OPC UA Endpoint")))
            .addField(nodeField))
        .example(QString::fromUtf8("抓取完整业务树快照"),
                 QStringList{"stdio", "console"},
                 QJsonObject{
                     {"host", "127.0.0.1"},
                     {"port", 4840}
                 });

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.opcua",
              "OPC UA Client",
              "1.0.0",
              QString::fromUtf8("基于 open62541 的 OPC UA 客户端驱动，"
                               "支持单节点查询与完整业务树快照"))
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description(QString::fromUtf8("获取驱动存活状态，固定返回 ready"))
            .returnField(FieldBuilder("result", FieldType::Object)
                .description(QString::fromUtf8("状态信息"))
                .addField(FieldBuilder("status", FieldType::String)
                    .description(QString::fromUtf8("固定返回 ready"))))
            .example(QString::fromUtf8("查询驱动状态"),
                     QStringList{"stdio", "console"},
                     QJsonObject{}))
        .command(inspectNode)
        .command(snapshotNodes)
        .build();
}

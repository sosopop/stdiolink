#include "driver_opcua_server/opcua_server_runtime.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThread>

#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
}

#include "opcua_common.h"

using namespace opcua_common;

namespace {

constexpr UA_UInt16 kCustomNamespaceIndex = 1;

thread_local bool g_suppressCommandWriteEvent = false;

QString normalizeEndpointPath(const QString& value) {
    QString path = value.trimmed();
    while (path.startsWith('/')) {
        path.remove(0, 1);
    }
    while (path.endsWith('/')) {
        path.chop(1);
    }
    return path;
}

QString buildEndpoint(const QString& bindHost, quint16 port, const QString& endpointPath) {
    const QString base = QString("opc.tcp://%1:%2").arg(bindHost).arg(port);
    const QString normalizedPath = normalizeEndpointPath(endpointPath);
    return normalizedPath.isEmpty() ? base : QString("%1/%2").arg(base, normalizedPath);
}

bool isSessionEventMode(const QString& eventMode) {
    return eventMode == "session" || eventMode == "all";
}

bool isWriteEventMode(const QString& eventMode) {
    return eventMode == "write" || eventMode == "all";
}

struct NodeSpec {
    QString nodeId;
    QString parentNodeId;
    QString nodeClass;
    QString browseName;
    QString displayName;
    QString description;
    QString dataType;
    QString access;
    bool hasInitialValue = false;
    QJsonValue initialValue;
};

struct NodeEntry {
    void* runtime = nullptr;
    QString nodeId;
    QString parentNodeId;
    QString nodeClass;
    QString browseName;
    QString displayName;
    QString description;
    QString dataType;
    QString access;
    QJsonValue currentValue;
};

struct AccessControlBridge {
    void* runtime = nullptr;
    void* innerContext = nullptr;
    void (*innerClear)(UA_AccessControl*) = nullptr;
    UA_StatusCode (*innerActivateSession)(UA_Server*,
                                          UA_AccessControl*,
                                          const UA_EndpointDescription*,
                                          const UA_ByteString*,
                                          const UA_NodeId*,
                                          const UA_ExtensionObject*,
                                          void**) = nullptr;
    void (*innerCloseSession)(UA_Server*,
                              UA_AccessControl*,
                              const UA_NodeId*,
                              void*) = nullptr;
};

bool readNodeClass(UA_Server* server,
                   const UA_NodeId& nodeId,
                   UA_NodeClass& nodeClass,
                   QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readNodeClass(server, nodeId, &nodeClass);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read NodeClass failed", statusCode);
        return false;
    }
    return true;
}

bool readBrowseName(UA_Server* server,
                    const UA_NodeId& nodeId,
                    UA_QualifiedName& browseName,
                    QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readBrowseName(server, nodeId, &browseName);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read BrowseName failed", statusCode);
        return false;
    }
    return true;
}

bool readDisplayName(UA_Server* server,
                     const UA_NodeId& nodeId,
                     UA_LocalizedText& displayName,
                     QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readDisplayName(server, nodeId, &displayName);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read DisplayName failed", statusCode);
        return false;
    }
    return true;
}

bool readDescription(UA_Server* server,
                     const UA_NodeId& nodeId,
                     UA_LocalizedText& description,
                     QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readDescription(server, nodeId, &description);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read Description failed", statusCode);
        return false;
    }
    return true;
}

bool readDataType(UA_Server* server,
                  const UA_NodeId& nodeId,
                  UA_NodeId& dataTypeId,
                  QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readDataType(server, nodeId, &dataTypeId);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read DataType failed", statusCode);
        return false;
    }
    return true;
}

bool readValueRank(UA_Server* server,
                   const UA_NodeId& nodeId,
                   UA_Int32& valueRank,
                   QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readValueRank(server, nodeId, &valueRank);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read ValueRank failed", statusCode);
        return false;
    }
    return true;
}

bool readAccessLevel(UA_Server* server,
                     const UA_NodeId& nodeId,
                     UA_Byte& accessLevel,
                     QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readAccessLevel(server, nodeId, &accessLevel);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read AccessLevel failed", statusCode);
        return false;
    }
    return true;
}

bool readValue(UA_Server* server,
               const UA_NodeId& nodeId,
               UA_Variant& value,
               QString& errorMessage) {
    const UA_StatusCode statusCode = UA_Server_readValue(server, nodeId, &value);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Read Value failed", statusCode);
        return false;
    }
    return true;
}

bool parseNodeSpec(const QJsonObject& object, NodeSpec& spec, QString& errorMessage) {
    QString normalizedNodeId;
    UA_NodeId parsedNodeId;
    if (!parseNodeIdString(object.value("node_id").toString(),
                           parsedNodeId,
                           normalizedNodeId,
                           &errorMessage)) {
        return false;
    }
    UA_NodeId_clear(&parsedNodeId);
    spec.nodeId = normalizedNodeId;

    QString normalizedParentNodeId;
    UA_NodeId parsedParentNodeId;
    if (!parseNodeIdString(object.value("parent_node_id").toString(),
                           parsedParentNodeId,
                           normalizedParentNodeId,
                           &errorMessage)) {
        errorMessage = QString("parent_node_id invalid: %1").arg(errorMessage);
        return false;
    }
    UA_NodeId_clear(&parsedParentNodeId);
    spec.parentNodeId = normalizedParentNodeId;

    spec.nodeClass = object.value("node_class").toString().trimmed().toLower();
    if (spec.nodeClass != "folder" && spec.nodeClass != "variable") {
        errorMessage = "node_class must be folder or variable";
        return false;
    }

    spec.browseName = object.value("browse_name").toString().trimmed();
    if (spec.browseName.isEmpty()) {
        errorMessage = "browse_name is required";
        return false;
    }

    spec.displayName = object.value("display_name").toString();
    if (spec.displayName.isEmpty()) {
        spec.displayName = spec.browseName;
    }
    spec.description = object.value("description").toString();

    if (spec.nodeClass == "variable") {
        spec.dataType = object.value("data_type").toString().trimmed().toLower();
        if (spec.dataType.isEmpty()) {
            errorMessage = "data_type is required for variable nodes";
            return false;
        }
        spec.access = object.value("access").toString().trimmed().toLower();
        if (spec.access != "read_only" && spec.access != "read_write") {
            errorMessage = "access must be read_only or read_write";
            return false;
        }
        if (object.contains("initial_value")) {
            spec.hasInitialValue = true;
            spec.initialValue = object.value("initial_value");
        }
    }

    return true;
}

UA_Byte accessLevelForMode(const QString& access) {
    if (access == "read_write") {
        return UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    }
    return UA_ACCESSLEVELMASK_READ;
}

bool parseNodeIdOwned(const QString& text,
                      UA_NodeId& outNodeId,
                      QString& errorMessage) {
    QString normalized;
    return parseNodeIdString(text, outNodeId, normalized, &errorMessage);
}

bool nodeExists(UA_Server* server,
                const QString& nodeIdText,
                UA_NodeClass* nodeClassOut,
                QString* errorMessage = nullptr) {
    UA_NodeId nodeId;
    QString parseError;
    if (!parseNodeIdOwned(nodeIdText, nodeId, parseError)) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return false;
    }

    UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
    const UA_StatusCode statusCode = UA_Server_readNodeClass(server, nodeId, &nodeClass);
    UA_NodeId_clear(&nodeId);
    if (statusCode == UA_STATUSCODE_BADNODEIDUNKNOWN) {
        return false;
    }
    if (statusCode != UA_STATUSCODE_GOOD) {
        if (errorMessage) {
            *errorMessage = formatStatusMessage("Read NodeClass failed", statusCode);
        }
        return false;
    }

    if (nodeClassOut) {
        *nodeClassOut = nodeClass;
    }
    return true;
}

bool updateLocalizedAttribute(UA_Server* server,
                              const QString& nodeIdText,
                              const QString& value,
                              bool isDisplayName,
                              QString& errorMessage) {
    UA_NodeId nodeId;
    if (!parseNodeIdOwned(nodeIdText, nodeId, errorMessage)) {
        return false;
    }

    const QByteArray valueUtf8 = value.toUtf8();
    UA_LocalizedText localized = UA_LOCALIZEDTEXT_ALLOC("en-US", valueUtf8.constData());
    const UA_StatusCode statusCode = isDisplayName
        ? UA_Server_writeDisplayName(server, nodeId, localized)
        : UA_Server_writeDescription(server, nodeId, localized);
    UA_LocalizedText_clear(&localized);
    UA_NodeId_clear(&nodeId);
    if (statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage(
            isDisplayName ? "Write DisplayName failed" : "Write Description failed",
            statusCode);
        return false;
    }
    return true;
}

bool collectDescendantNodeIds(UA_Server* server,
                              const UA_NodeId& nodeId,
                              bool recursive,
                              QSet<QString>& collected,
                              QString& errorMessage) {
    const QString normalized = nodeIdToQString(nodeId);
    if (collected.contains(normalized)) {
        return true;
    }
    collected.insert(normalized);

    UA_BrowseDescription description;
    UA_BrowseDescription_init(&description);
    description.nodeId = nodeId;
    description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    description.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    description.includeSubtypes = true;
    description.resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResult result = UA_Server_browse(server, 0, &description);
    if (result.statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Browse failed", result.statusCode);
        UA_BrowseResult_clear(&result);
        return false;
    }

    auto appendReferences = [&](const UA_BrowseResult& browseResult) -> bool {
        for (size_t i = 0; i < browseResult.referencesSize; ++i) {
            const UA_ReferenceDescription& reference = browseResult.references[i];
            if (!UA_ExpandedNodeId_isLocal(&reference.nodeId)) {
                continue;
            }
            const UA_NodeId& childNodeId = reference.nodeId.nodeId;
            if (recursive) {
                if (!collectDescendantNodeIds(server, childNodeId, true, collected, errorMessage)) {
                    return false;
                }
            } else {
                collected.insert(nodeIdToQString(childNodeId));
            }
        }
        return true;
    };

    if (!appendReferences(result)) {
        UA_BrowseResult_clear(&result);
        return false;
    }

    UA_ByteString continuationPoint = UA_BYTESTRING_NULL;
    if (result.continuationPoint.length > 0) {
        const UA_StatusCode copyStatus =
            UA_ByteString_copy(&result.continuationPoint, &continuationPoint);
        if (copyStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
            UA_BrowseResult_clear(&result);
            return false;
        }
    }
    UA_BrowseResult_clear(&result);

    while (continuationPoint.length > 0) {
        UA_BrowseResult next = UA_Server_browseNext(server, false, &continuationPoint);
        UA_ByteString_clear(&continuationPoint);
        if (next.statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("BrowseNext failed", next.statusCode);
            UA_BrowseResult_clear(&next);
            return false;
        }
        if (!appendReferences(next)) {
            UA_BrowseResult_clear(&next);
            return false;
        }
        if (next.continuationPoint.length > 0) {
            const UA_StatusCode copyStatus =
                UA_ByteString_copy(&next.continuationPoint, &continuationPoint);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
                UA_BrowseResult_clear(&next);
                return false;
            }
        }
        UA_BrowseResult_clear(&next);
    }

    return true;
}

bool browseChildren(UA_Server* server,
                    const UA_NodeId& nodeId,
                    bool recurseChildren,
                    bool excludeNamespaceZeroChildren,
                    QSet<QString>& visited,
                    QJsonArray& outChildren,
                    QString& errorMessage);

bool readNodeSnapshot(UA_Server* server,
                      const UA_NodeId& nodeId,
                      bool expandChildren,
                      bool recurseChildren,
                      bool excludeNamespaceZeroChildren,
                      QSet<QString>& visited,
                      QJsonObject& outNode,
                      QString& errorMessage) {
    UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
    if (!readNodeClass(server, nodeId, nodeClass, errorMessage)) {
        return false;
    }

    UA_QualifiedName browseName;
    if (!readBrowseName(server, nodeId, browseName, errorMessage)) {
        return false;
    }
    UA_LocalizedText displayName;
    if (!readDisplayName(server, nodeId, displayName, errorMessage)) {
        UA_QualifiedName_clear(&browseName);
        return false;
    }
    UA_LocalizedText description;
    if (!readDescription(server, nodeId, description, errorMessage)) {
        UA_QualifiedName_clear(&browseName);
        UA_LocalizedText_clear(&displayName);
        return false;
    }

    const QString normalizedNodeId = nodeIdToQString(nodeId);
    const bool alreadyVisited = visited.contains(normalizedNodeId);
    visited.insert(normalizedNodeId);

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
        UA_Int32 valueRank = -1;
        UA_Byte accessLevel = 0;
        UA_Variant value;
        UA_NodeId_init(&dataTypeId);
        UA_Variant_init(&value);

        const bool success = readDataType(server, nodeId, dataTypeId, errorMessage)
            && readValueRank(server, nodeId, valueRank, errorMessage)
            && readAccessLevel(server, nodeId, accessLevel, errorMessage)
            && readValue(server, nodeId, value, errorMessage);
        if (!success) {
            UA_NodeId_clear(&dataTypeId);
            UA_Variant_clear(&value);
            UA_QualifiedName_clear(&browseName);
            UA_LocalizedText_clear(&displayName);
            UA_LocalizedText_clear(&description);
            return false;
        }

        const QJsonValue jsonValue = variantToJson(value);
        node["data_type_id"] = nodeIdToQString(dataTypeId);
        node["data_type_name"] = dataTypeNameFromNodeId(dataTypeId);
        node["value_rank"] = valueRank;
        node["access_level"] = static_cast<int>(accessLevel);
        node["value"] = jsonValue;
        node["value_text"] = jsonValueToText(jsonValue);

        UA_NodeId_clear(&dataTypeId);
        UA_Variant_clear(&value);
    }

    if (!alreadyVisited && expandChildren && isDirectoryNodeClass(nodeClass)) {
        QJsonArray children;
        if (!browseChildren(server,
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

bool browseChildren(UA_Server* server,
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

    UA_BrowseResult result = UA_Server_browse(server, 0, &description);
    if (result.statusCode != UA_STATUSCODE_GOOD) {
        errorMessage = formatStatusMessage("Browse failed", result.statusCode);
        UA_BrowseResult_clear(&result);
        return false;
    }

    auto appendReferences = [&](const UA_BrowseResult& browseResult) -> bool {
        for (size_t i = 0; i < browseResult.referencesSize; ++i) {
            const UA_ReferenceDescription& reference = browseResult.references[i];
            if (!UA_ExpandedNodeId_isLocal(&reference.nodeId)) {
                continue;
            }

            const UA_NodeId& localNodeId = reference.nodeId.nodeId;
            if (excludeNamespaceZeroChildren && localNodeId.namespaceIndex == 0) {
                continue;
            }

            UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
            if (!readNodeClass(server, localNodeId, nodeClass, errorMessage)) {
                return false;
            }
            if (!isBrowsableSnapshotNode(nodeClass)) {
                continue;
            }

            UA_NodeId childNodeId;
            UA_NodeId_init(&childNodeId);
            const UA_StatusCode copyStatus = UA_NodeId_copy(&localNodeId, &childNodeId);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("Copy child NodeId failed", copyStatus);
                return false;
            }

            QJsonObject childNode;
            if (!readNodeSnapshot(server,
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

    if (!appendReferences(result)) {
        UA_BrowseResult_clear(&result);
        return false;
    }

    UA_ByteString continuationPoint = UA_BYTESTRING_NULL;
    if (result.continuationPoint.length > 0) {
        const UA_StatusCode copyStatus =
            UA_ByteString_copy(&result.continuationPoint, &continuationPoint);
        if (copyStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
            UA_BrowseResult_clear(&result);
            return false;
        }
    }
    UA_BrowseResult_clear(&result);

    while (continuationPoint.length > 0) {
        UA_BrowseResult next = UA_Server_browseNext(server, false, &continuationPoint);
        UA_ByteString_clear(&continuationPoint);
        if (next.statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("BrowseNext failed", next.statusCode);
            UA_BrowseResult_clear(&next);
            return false;
        }
        if (!appendReferences(next)) {
            UA_BrowseResult_clear(&next);
            return false;
        }
        if (next.continuationPoint.length > 0) {
            const UA_StatusCode copyStatus =
                UA_ByteString_copy(&next.continuationPoint, &continuationPoint);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("Copy continuation point failed", copyStatus);
                UA_BrowseResult_clear(&next);
                return false;
            }
        }
        UA_BrowseResult_clear(&next);
    }

    return true;
}

} // namespace

struct OpcUaServerRuntime::Impl {
    UA_Server* server = nullptr;
    QString endpointValue;
    QString namespaceUriValue;
    QString eventModeValue = "write";
    UA_UInt16 namespaceIndexValue = 0;
    std::atomic<bool> threadRunning{false};
    std::thread iterateThread;
    EventCallback eventCallback;
    std::mutex stateMutex;
    std::map<QString, std::unique_ptr<NodeEntry>> nodeEntries;

    ~Impl() {
        QString errorMessage;
        stop(0, errorMessage);
    }

    void emitEvent(const QString& name, const QJsonObject& data) {
        if (eventCallback) {
            eventCallback(name, data);
        }
    }

    NodeEntry* findEntry(const QString& nodeId) {
        const auto it = nodeEntries.find(nodeId);
        return it == nodeEntries.end() ? nullptr : it->second.get();
    }

    NodeEntry* ensureEntry(const NodeSpec& spec) {
        auto it = nodeEntries.find(spec.nodeId);
        if (it == nodeEntries.end()) {
            auto entry = std::make_unique<NodeEntry>();
            entry->runtime = this;
            entry->nodeId = spec.nodeId;
            entry->parentNodeId = spec.parentNodeId;
            entry->nodeClass = spec.nodeClass;
            entry->browseName = spec.browseName;
            entry->displayName = spec.displayName;
            entry->description = spec.description;
            entry->dataType = spec.dataType;
            entry->access = spec.access;
            const auto inserted = nodeEntries.emplace(spec.nodeId, std::move(entry));
            return inserted.first->second.get();
        }

        NodeEntry* entry = it->second.get();
        entry->parentNodeId = spec.parentNodeId;
        entry->nodeClass = spec.nodeClass;
        entry->browseName = spec.browseName;
        entry->displayName = spec.displayName;
        entry->description = spec.description;
        entry->dataType = spec.dataType;
        entry->access = spec.access;
        return entry;
    }

    static void accessControlClear(UA_AccessControl* accessControl) {
        auto* bridge = static_cast<AccessControlBridge*>(accessControl->context);
        if (!bridge) {
            return;
        }

        accessControl->context = bridge->innerContext;
        accessControl->clear = bridge->innerClear;
        accessControl->activateSession = bridge->innerActivateSession;
        accessControl->closeSession = bridge->innerCloseSession;
        if (bridge->innerClear) {
            bridge->innerClear(accessControl);
        }
        delete bridge;
    }

    static UA_StatusCode accessControlActivateSession(UA_Server* serverHandle,
                                                      UA_AccessControl* accessControl,
                                                      const UA_EndpointDescription* endpointDescription,
                                                      const UA_ByteString* remoteCertificate,
                                                      const UA_NodeId* sessionId,
                                                      const UA_ExtensionObject* userIdentityToken,
                                                      void** sessionContext) {
        auto* bridge = static_cast<AccessControlBridge*>(accessControl->context);
        if (!bridge || !bridge->innerActivateSession) {
            return UA_STATUSCODE_BADINTERNALERROR;
        }

        accessControl->context = bridge->innerContext;
        const UA_StatusCode statusCode =
            bridge->innerActivateSession(serverHandle,
                                         accessControl,
                                         endpointDescription,
                                         remoteCertificate,
                                         sessionId,
                                         userIdentityToken,
                                         sessionContext);
        accessControl->context = bridge;
        auto* runtime = static_cast<Impl*>(bridge->runtime);
        if (statusCode == UA_STATUSCODE_GOOD && runtime
            && isSessionEventMode(runtime->eventModeValue)) {
            runtime->emitEvent("session_activated", QJsonObject{
                {"session_id", sessionId ? nodeIdToQString(*sessionId) : QString{}}
            });
        }
        return statusCode;
    }

    static void accessControlCloseSession(UA_Server* serverHandle,
                                          UA_AccessControl* accessControl,
                                          const UA_NodeId* sessionId,
                                          void* sessionContext) {
        auto* bridge = static_cast<AccessControlBridge*>(accessControl->context);
        if (!bridge) {
            return;
        }

        const QString sessionIdText = sessionId ? nodeIdToQString(*sessionId) : QString{};
        accessControl->context = bridge->innerContext;
        if (bridge->innerCloseSession) {
            bridge->innerCloseSession(serverHandle, accessControl, sessionId, sessionContext);
        }
        accessControl->context = bridge;
        auto* runtime = static_cast<Impl*>(bridge->runtime);
        if (runtime && isSessionEventMode(runtime->eventModeValue)) {
            runtime->emitEvent("session_closed", QJsonObject{{"session_id", sessionIdText}});
        }
    }

    static void variableWritten(UA_Server* serverHandle,
                                const UA_NodeId* sessionId,
                                void* sessionContext,
                                const UA_NodeId* nodeId,
                                void* nodeContext,
                                const UA_NumericRange* range,
                                const UA_DataValue* data) {
        (void)serverHandle;
        (void)sessionContext;
        (void)range;
        (void)nodeId;
        if (g_suppressCommandWriteEvent || !nodeContext || !data || !data->hasValue) {
            return;
        }

        auto* entry = static_cast<NodeEntry*>(nodeContext);
        auto* runtime = static_cast<Impl*>(entry->runtime);
        if (!runtime || !isWriteEventMode(runtime->eventModeValue)) {
            return;
        }

        const QJsonValue newValue = variantToJson(data->value);
        QJsonValue oldValue;
        {
            std::lock_guard<std::mutex> lock(runtime->stateMutex);
            oldValue = entry->currentValue;
            entry->currentValue = newValue;
        }

        runtime->emitEvent("node_value_changed", QJsonObject{
            {"node_id", entry->nodeId},
            {"display_name", entry->displayName},
            {"old_value", oldValue},
            {"value", newValue},
            {"source", sessionId ? "external_write" : "command_write"}
        });
    }

    bool configureAccessControl(UA_ServerConfig* config) {
        auto* bridge = new AccessControlBridge();
        bridge->runtime = this;
        bridge->innerContext = config->accessControl.context;
        bridge->innerClear = config->accessControl.clear;
        bridge->innerActivateSession = config->accessControl.activateSession;
        bridge->innerCloseSession = config->accessControl.closeSession;

        config->accessControl.context = bridge;
        config->accessControl.clear = &Impl::accessControlClear;
        config->accessControl.activateSession = &Impl::accessControlActivateSession;
        config->accessControl.closeSession = &Impl::accessControlCloseSession;
        return true;
    }

    bool setServerUrls(UA_ServerConfig* config, const QString& endpointText, QString& errorMessage) {
        const QByteArray endpointUtf8 = endpointText.toUtf8();

        for (size_t i = 0; i < config->serverUrlsSize; ++i) {
            UA_String_clear(&config->serverUrls[i]);
        }
        if (config->serverUrls) {
            UA_Array_delete(config->serverUrls, config->serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
        }
        config->serverUrls =
            static_cast<UA_String*>(UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]));
        if (!config->serverUrls) {
            errorMessage = "failed to allocate serverUrls";
            config->serverUrlsSize = 0;
            return false;
        }
        config->serverUrlsSize = 1;
        config->serverUrls[0] = UA_STRING_ALLOC(endpointUtf8.constData());

        for (size_t i = 0; i < config->endpointsSize; ++i) {
            UA_String_clear(&config->endpoints[i].endpointUrl);
            config->endpoints[i].endpointUrl = UA_STRING_ALLOC(endpointUtf8.constData());
        }
        return true;
    }

    bool start(const StartOptions& options, QString& errorMessage) {
        if (server) {
            errorMessage = "server already running";
            return false;
        }

        endpointValue = buildEndpoint(options.bindHost, options.listenPort, options.endpointPath);
        namespaceUriValue = options.namespaceUri;
        eventModeValue = options.eventMode;

        auto* config = static_cast<UA_ServerConfig*>(UA_calloc(1, sizeof(UA_ServerConfig)));
        if (!config) {
            errorMessage = "failed to allocate UA_ServerConfig";
            return false;
        }

        config->logging = silentLogger();
        UA_StatusCode statusCode = UA_ServerConfig_setMinimal(config, options.listenPort, nullptr);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_ServerConfig_setMinimal failed", statusCode);
            UA_ServerConfig_clean(config);
            UA_free(config);
            return false;
        }
        config->logging = silentLogger();

        const QByteArray serverNameUtf8 = options.serverName.toUtf8();
        const QByteArray applicationUriUtf8 = options.applicationUri.toUtf8();
        UA_LocalizedText_clear(&config->applicationDescription.applicationName);
        config->applicationDescription.applicationName =
            UA_LOCALIZEDTEXT_ALLOC("en-US", serverNameUtf8.constData());
        UA_String_clear(&config->applicationDescription.applicationUri);
        config->applicationDescription.applicationUri = UA_STRING_ALLOC(applicationUriUtf8.constData());

        if (!setServerUrls(config, endpointValue, errorMessage)) {
            UA_ServerConfig_clean(config);
            UA_free(config);
            return false;
        }

        configureAccessControl(config);

        server = UA_Server_newWithConfig(config);
        if (!server) {
            errorMessage = "failed to create UA_Server";
            UA_ServerConfig_clean(config);
            UA_free(config);
            return false;
        }

        namespaceIndexValue = kCustomNamespaceIndex;

        statusCode = UA_Server_run_startup(server);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Server_run_startup failed", statusCode);
            UA_Server_delete(server);
            server = nullptr;
            namespaceIndexValue = 0;
            return false;
        }

        threadRunning.store(true);
        iterateThread = std::thread([this]() {
            while (threadRunning.load()) {
                UA_Server_run_iterate(server, false);
                QThread::msleep(5);
            }
        });
        return true;
    }

    bool stop(int gracefulTimeoutMs, QString& errorMessage) {
        (void)gracefulTimeoutMs;
        if (!server) {
            errorMessage = "server not running";
            return false;
        }

        threadRunning.store(false);
        if (iterateThread.joinable()) {
            iterateThread.join();
        }

        UA_Server_run_shutdown(server);
        UA_Server_delete(server);
        server = nullptr;
        namespaceIndexValue = 0;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            nodeEntries.clear();
        }

        endpointValue.clear();
        namespaceUriValue.clear();
        return true;
    }

    bool updateEntryContext(const QString& nodeIdText, NodeEntry* entry, QString& errorMessage) {
        UA_NodeId nodeId;
        if (!parseNodeIdOwned(nodeIdText, nodeId, errorMessage)) {
            return false;
        }
        const UA_StatusCode statusCode = UA_Server_setNodeContext(server, nodeId, entry);
        UA_NodeId_clear(&nodeId);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Server_setNodeContext failed", statusCode);
            return false;
        }
        return true;
    }

    bool installValueCallback(const QString& nodeIdText, QString& errorMessage) {
        UA_NodeId nodeId;
        if (!parseNodeIdOwned(nodeIdText, nodeId, errorMessage)) {
            return false;
        }
        UA_ValueCallback callback{};
        callback.onWrite = &Impl::variableWritten;
        const UA_StatusCode statusCode =
            UA_Server_setVariableNode_valueCallback(server, nodeId, callback);
        UA_NodeId_clear(&nodeId);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage(
                "UA_Server_setVariableNode_valueCallback failed",
                statusCode);
            return false;
        }
        return true;
    }

    bool createFolderNode(const NodeSpec& spec, QString& errorMessage) {
        UA_NodeId nodeId;
        UA_NodeId parentNodeId;
        if (!parseNodeIdOwned(spec.nodeId, nodeId, errorMessage)
            || !parseNodeIdOwned(spec.parentNodeId, parentNodeId, errorMessage)) {
            UA_NodeId_clear(&nodeId);
            UA_NodeId_clear(&parentNodeId);
            return false;
        }

        const QByteArray browseNameUtf8 = spec.browseName.toUtf8();
        const QByteArray displayNameUtf8 = spec.displayName.toUtf8();
        const QByteArray descriptionUtf8 = spec.description.toUtf8();
        UA_ObjectAttributes attributes = UA_ObjectAttributes_default;
        attributes.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", displayNameUtf8.constData());
        attributes.description = UA_LOCALIZEDTEXT_ALLOC("en-US", descriptionUtf8.constData());

        const UA_StatusCode statusCode =
            UA_Server_addObjectNode(server,
                                    nodeId,
                                    parentNodeId,
                                    UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                    UA_QUALIFIEDNAME(kCustomNamespaceIndex,
                                                     const_cast<char*>(browseNameUtf8.constData())),
                                    UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                                    attributes,
                                    nullptr,
                                    nullptr);
        UA_ObjectAttributes_clear(&attributes);
        UA_NodeId_clear(&nodeId);
        UA_NodeId_clear(&parentNodeId);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Server_addObjectNode failed", statusCode);
            return false;
        }
        return true;
    }

    bool createVariableNode(const NodeSpec& spec, QString& errorMessage) {
        if (!spec.hasInitialValue) {
            errorMessage = "initial_value is required when creating a variable node";
            return false;
        }

        const UA_DataType* dataType = nullptr;
        UA_NodeId dataTypeId = UA_NODEID_NULL;
        if (!resolveBuiltinDataType(spec.dataType, dataType, dataTypeId, &errorMessage)) {
            return false;
        }

        OpcUaVariantStorage storage;
        if (!jsonToVariant(spec.dataType, spec.initialValue, true, storage, &errorMessage)) {
            return false;
        }

        UA_NodeId nodeId;
        UA_NodeId parentNodeId;
        if (!parseNodeIdOwned(spec.nodeId, nodeId, errorMessage)
            || !parseNodeIdOwned(spec.parentNodeId, parentNodeId, errorMessage)) {
            UA_NodeId_clear(&nodeId);
            UA_NodeId_clear(&parentNodeId);
            return false;
        }

        const QByteArray browseNameUtf8 = spec.browseName.toUtf8();
        const QByteArray displayNameUtf8 = spec.displayName.toUtf8();
        const QByteArray descriptionUtf8 = spec.description.toUtf8();
        UA_VariableAttributes attributes = UA_VariableAttributes_default;
        const UA_StatusCode copyStatus =
            UA_Variant_copy(&storage.variant, &attributes.value);
        if (copyStatus != UA_STATUSCODE_GOOD) {
            UA_NodeId_clear(&nodeId);
            UA_NodeId_clear(&parentNodeId);
            errorMessage = formatStatusMessage("UA_Variant_copy failed", copyStatus);
            return false;
        }
        attributes.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", displayNameUtf8.constData());
        attributes.description = UA_LOCALIZEDTEXT_ALLOC("en-US", descriptionUtf8.constData());
        attributes.dataType = dataTypeId;
        attributes.valueRank = -1;
        attributes.accessLevel = accessLevelForMode(spec.access);

        const UA_StatusCode statusCode =
            UA_Server_addVariableNode(server,
                                      nodeId,
                                      parentNodeId,
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                      UA_QUALIFIEDNAME(kCustomNamespaceIndex,
                                                       const_cast<char*>(browseNameUtf8.constData())),
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                      attributes,
                                      nullptr,
                                      nullptr);
        UA_VariableAttributes_clear(&attributes);
        UA_NodeId_clear(&nodeId);
        UA_NodeId_clear(&parentNodeId);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Server_addVariableNode failed", statusCode);
            return false;
        }
        return true;
    }

    bool updateExistingVariable(const NodeSpec& spec, QString& errorMessage) {
        UA_NodeId nodeId;
        if (!parseNodeIdOwned(spec.nodeId, nodeId, errorMessage)) {
            return false;
        }

        UA_NodeId actualDataType = UA_NODEID_NULL;
        const UA_DataType* expectedDataType = nullptr;
        UA_NodeId expectedDataTypeId = UA_NODEID_NULL;
        if (!resolveBuiltinDataType(spec.dataType,
                                    expectedDataType,
                                    expectedDataTypeId,
                                    &errorMessage)) {
            UA_NodeId_clear(&nodeId);
            return false;
        }

        const UA_StatusCode dataTypeStatus = UA_Server_readDataType(server, nodeId, &actualDataType);
        if (dataTypeStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Read DataType failed", dataTypeStatus);
            UA_NodeId_clear(&nodeId);
            return false;
        }
        if (!UA_NodeId_equal(&actualDataType, &expectedDataTypeId)) {
            errorMessage = QString("existing variable data_type mismatch for %1").arg(spec.nodeId);
            UA_NodeId_clear(&actualDataType);
            UA_NodeId_clear(&nodeId);
            return false;
        }
        UA_NodeId_clear(&actualDataType);

        const UA_StatusCode accessStatus =
            UA_Server_writeAccessLevel(server, nodeId, accessLevelForMode(spec.access));
        if (accessStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("Write AccessLevel failed", accessStatus);
            UA_NodeId_clear(&nodeId);
            return false;
        }

        if (spec.hasInitialValue) {
            OpcUaVariantStorage storage;
            if (!jsonToVariant(spec.dataType, spec.initialValue, true, storage, &errorMessage)) {
                return false;
            }

            UA_DataValue dataValue;
            UA_DataValue_init(&dataValue);
            const UA_StatusCode copyStatus = UA_Variant_copy(&storage.variant, &dataValue.value);
            if (copyStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("UA_Variant_copy failed", copyStatus);
                UA_DataValue_clear(&dataValue);
                return false;
            }
            dataValue.hasValue = true;

            g_suppressCommandWriteEvent = true;
            const UA_StatusCode writeStatus = UA_Server_writeDataValue(server, nodeId, dataValue);
            g_suppressCommandWriteEvent = false;
            UA_DataValue_clear(&dataValue);
            if (writeStatus != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("UA_Server_writeDataValue failed", writeStatus);
                UA_NodeId_clear(&nodeId);
                return false;
            }
        }
        UA_NodeId_clear(&nodeId);
        return true;
    }

    bool upsertNode(const NodeSpec& spec, bool& created, QString& errorMessage) {
        created = false;
        UA_NodeClass existingClass = UA_NODECLASS_UNSPECIFIED;
        QString existsError;
        const bool exists = nodeExists(server, spec.nodeId, &existingClass, &existsError);
        if (!existsError.isEmpty()) {
            errorMessage = existsError;
            return false;
        }

        if (exists) {
            const bool classMatches =
                (spec.nodeClass == "folder" && isDirectoryNodeClass(existingClass))
                || (spec.nodeClass == "variable" && existingClass == UA_NODECLASS_VARIABLE);
            if (!classMatches) {
                errorMessage = QString("node_class mismatch for existing node %1").arg(spec.nodeId);
                return false;
            }
        } else {
            if (spec.nodeClass == "folder") {
                if (!createFolderNode(spec, errorMessage)) {
                    return false;
                }
            } else {
                if (!createVariableNode(spec, errorMessage)) {
                    return false;
                }
            }
            created = true;
        }

        if (!updateLocalizedAttribute(server, spec.nodeId, spec.displayName, true, errorMessage)) {
            return false;
        }
        if (!updateLocalizedAttribute(server, spec.nodeId, spec.description, false, errorMessage)) {
            return false;
        }
        if (spec.nodeClass == "variable" && !updateExistingVariable(spec, errorMessage)) {
            return false;
        }

        NodeEntry* entry = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            entry = ensureEntry(spec);
            if (spec.nodeClass == "variable" && spec.hasInitialValue) {
                entry->currentValue = spec.initialValue;
            }
        }
        if (!updateEntryContext(spec.nodeId, entry, errorMessage)) {
            return false;
        }
        if (spec.nodeClass == "variable" && !installValueCallback(spec.nodeId, errorMessage)) {
            return false;
        }
        return true;
    }

    bool upsertNodes(const QJsonArray& nodes, QJsonArray& results, QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }

        QList<NodeSpec> pending;
        pending.reserve(nodes.size());
        for (int i = 0; i < nodes.size(); ++i) {
            if (!nodes.at(i).isObject()) {
                errorMessage = QString("nodes[%1] must be an object").arg(i);
                return false;
            }
            NodeSpec spec;
            if (!parseNodeSpec(nodes.at(i).toObject(), spec, errorMessage)) {
                errorMessage = QString("nodes[%1]: %2").arg(i).arg(errorMessage);
                return false;
            }
            pending.append(spec);
        }

        QList<NodeSpec> remaining = pending;
        QStringList createdNodeIds;
        QJsonArray localResults;

        while (!remaining.isEmpty()) {
            QList<NodeSpec> nextPass;
            int passProgress = 0;

            for (const NodeSpec& spec : remaining) {
                QString parentError;
                UA_NodeClass parentClass = UA_NODECLASS_UNSPECIFIED;
                const bool parentExists = nodeExists(server, spec.parentNodeId, &parentClass, &parentError);
                if (!parentExists) {
                    if (!parentError.isEmpty()) {
                        errorMessage = parentError;
                        goto rollback_failure;
                    }
                    nextPass.append(spec);
                    continue;
                }

                bool created = false;
                if (!upsertNode(spec, created, errorMessage)) {
                    goto rollback_failure;
                }

                if (created) {
                    createdNodeIds.append(spec.nodeId);
                }
                ++passProgress;
                localResults.append(QJsonObject{
                    {"node_id", spec.nodeId},
                    {"created", created},
                    {"updated", true}
                });
            }

            if (passProgress == 0) {
                QJsonArray unresolved;
                for (const NodeSpec& spec : nextPass) {
                    unresolved.append(spec.nodeId);
                }
                errorMessage = QString("unresolved parent_node_id for nodes: %1")
                                   .arg(QString::fromUtf8(QJsonDocument(unresolved).toJson(QJsonDocument::Compact)));
                goto rollback_failure;
            }
            remaining = nextPass;
        }

        results = localResults;
        return true;

rollback_failure:
        for (auto it = createdNodeIds.crbegin(); it != createdNodeIds.crend(); ++it) {
            UA_NodeId createdNodeId;
            QString rollbackError;
            if (!parseNodeIdOwned(*it, createdNodeId, rollbackError)) {
                continue;
            }
            UA_Server_deleteNode(server, createdNodeId, true);
            UA_NodeId_clear(&createdNodeId);
            std::lock_guard<std::mutex> lock(stateMutex);
            nodeEntries.erase(*it);
        }
        return false;
    }

    bool deleteNodes(const QStringList& nodeIds,
                     bool recursive,
                     QJsonArray& results,
                     QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }

        QJsonArray localResults;
        for (const QString& nodeIdText : nodeIds) {
            UA_NodeId nodeId;
            if (!parseNodeIdOwned(nodeIdText, nodeId, errorMessage)) {
                return false;
            }

            UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
            if (!readNodeClass(server, nodeId, nodeClass, errorMessage)) {
                UA_NodeId_clear(&nodeId);
                return false;
            }
            if (isDirectoryNodeClass(nodeClass) && !recursive) {
                errorMessage = QString("recursive=true is required to delete directory node %1")
                                   .arg(nodeIdText);
                UA_NodeId_clear(&nodeId);
                return false;
            }

            QSet<QString> descendants;
            if (!collectDescendantNodeIds(server, nodeId, recursive, descendants, errorMessage)) {
                UA_NodeId_clear(&nodeId);
                return false;
            }

            const UA_StatusCode statusCode = UA_Server_deleteNode(server, nodeId, recursive);
            UA_NodeId_clear(&nodeId);
            if (statusCode != UA_STATUSCODE_GOOD) {
                errorMessage = formatStatusMessage("UA_Server_deleteNode failed", statusCode);
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                for (const QString& id : descendants) {
                    nodeEntries.erase(id);
                }
            }

            localResults.append(QJsonObject{
                {"node_id", nodeIdText},
                {"deleted", true}
            });
        }

        results = localResults;
        return true;
    }

    bool writeSingleValue(const QString& nodeIdText,
                          const QJsonValue& value,
                          const QString& sourceTimestamp,
                          bool strictType,
                          QJsonObject& result,
                          QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }

        UA_NodeId nodeId;
        if (!parseNodeIdOwned(nodeIdText, nodeId, errorMessage)) {
            return false;
        }

        UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
        if (!readNodeClass(server, nodeId, nodeClass, errorMessage)) {
            UA_NodeId_clear(&nodeId);
            return false;
        }
        if (nodeClass != UA_NODECLASS_VARIABLE) {
            errorMessage = QString("%1 is not a variable node").arg(nodeIdText);
            UA_NodeId_clear(&nodeId);
            return false;
        }

        UA_Byte accessLevel = 0;
        if (!readAccessLevel(server, nodeId, accessLevel, errorMessage)) {
            UA_NodeId_clear(&nodeId);
            return false;
        }
        if ((accessLevel & UA_ACCESSLEVELMASK_WRITE) == 0) {
            errorMessage = QString("%1 is read_only").arg(nodeIdText);
            UA_NodeId_clear(&nodeId);
            return false;
        }

        UA_NodeId dataTypeId = UA_NODEID_NULL;
        if (!readDataType(server, nodeId, dataTypeId, errorMessage)) {
            UA_NodeId_clear(&nodeId);
            return false;
        }
        const QString dataTypeName = dataTypeNameFromNodeId(dataTypeId);
        OpcUaVariantStorage storage;
        if (!jsonToVariant(dataTypeName, value, strictType, storage, &errorMessage)) {
            UA_NodeId_clear(&dataTypeId);
            UA_NodeId_clear(&nodeId);
            return false;
        }

        UA_Variant oldValueVariant;
        UA_Variant_init(&oldValueVariant);
        if (!readValue(server, nodeId, oldValueVariant, errorMessage)) {
            UA_NodeId_clear(&dataTypeId);
            UA_NodeId_clear(&nodeId);
            return false;
        }
        const QJsonValue oldValue = variantToJson(oldValueVariant);
        UA_Variant_clear(&oldValueVariant);

        UA_DataValue dataValue;
        UA_DataValue_init(&dataValue);
        const UA_StatusCode copyStatus = UA_Variant_copy(&storage.variant, &dataValue.value);
        if (copyStatus != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Variant_copy failed", copyStatus);
            UA_NodeId_clear(&dataTypeId);
            UA_NodeId_clear(&nodeId);
            return false;
        }
        dataValue.hasValue = true;
        if (!sourceTimestamp.trimmed().isEmpty()) {
            QDateTime parsed = QDateTime::fromString(sourceTimestamp, Qt::ISODateWithMs);
            if (!parsed.isValid()) {
                parsed = QDateTime::fromString(sourceTimestamp, Qt::ISODate);
            }
            parsed = parsed.toUTC();
            if (!parsed.isValid()) {
                UA_DataValue_clear(&dataValue);
                UA_NodeId_clear(&dataTypeId);
                UA_NodeId_clear(&nodeId);
                errorMessage = "source_timestamp must be a valid ISO8601 string";
                return false;
            }
            dataValue.hasSourceTimestamp = true;
            dataValue.sourceTimestamp =
                UA_DATETIME_UNIX_EPOCH + parsed.toMSecsSinceEpoch() * UA_DATETIME_MSEC;
        }

        g_suppressCommandWriteEvent = true;
        const UA_StatusCode statusCode = UA_Server_writeDataValue(server, nodeId, dataValue);
        g_suppressCommandWriteEvent = false;
        UA_DataValue_clear(&dataValue);
        if (statusCode != UA_STATUSCODE_GOOD) {
            errorMessage = formatStatusMessage("UA_Server_writeDataValue failed", statusCode);
            UA_NodeId_clear(&dataTypeId);
            UA_NodeId_clear(&nodeId);
            return false;
        }

        NodeEntry* entry = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            entry = findEntry(nodeIdText);
            if (entry) {
                entry->currentValue = value;
            }
        }

        if (isWriteEventMode(eventModeValue)) {
            emitEvent("node_value_changed", QJsonObject{
                {"node_id", nodeIdText},
                {"display_name", entry ? entry->displayName : QString{}},
                {"old_value", oldValue},
                {"value", value},
                {"source", "command_write"}
            });
        }

        result = QJsonObject{
            {"node_id", nodeIdText},
            {"old_value", oldValue},
            {"value", value},
            {"data_type_name", dataTypeName}
        };
        UA_NodeId_clear(&dataTypeId);
        UA_NodeId_clear(&nodeId);
        return true;
    }

    bool writeValues(const QJsonArray& items,
                     bool strictType,
                     QJsonArray& results,
                     QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }

        QJsonArray localResults;
        for (int i = 0; i < items.size(); ++i) {
            if (!items.at(i).isObject()) {
                errorMessage = QString("items[%1] must be an object").arg(i);
                return false;
            }

            const QJsonObject item = items.at(i).toObject();
            const QString nodeIdText = item.value("node_id").toString();
            if (nodeIdText.trimmed().isEmpty()) {
                errorMessage = QString("items[%1].node_id is required").arg(i);
                return false;
            }
            if (!item.contains("value")) {
                errorMessage = QString("items[%1].value is required").arg(i);
                return false;
            }

            QJsonObject result;
            if (!writeSingleValue(nodeIdText,
                                  item.value("value"),
                                  item.value("source_timestamp").toString(),
                                  strictType,
                                  result,
                                  errorMessage)) {
                errorMessage = QString("items[%1]: %2").arg(i).arg(errorMessage);
                return false;
            }
            localResults.append(result);
        }

        results = localResults;
        return true;
    }

    bool inspectNode(const QString& nodeIdText,
                     bool recurse,
                     QJsonObject& node,
                     QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }
        UA_NodeId nodeId;
        QString normalized;
        if (!parseNodeIdString(nodeIdText, nodeId, normalized, &errorMessage)) {
            return false;
        }
        QSet<QString> visited;
        const bool ok =
            readNodeSnapshot(server, nodeId, true, recurse, false, visited, node, errorMessage);
        UA_NodeId_clear(&nodeId);
        return ok;
    }

    bool snapshotNodes(const QString& rootNodeIdText,
                       bool recurse,
                       QJsonObject& node,
                       QString& errorMessage) {
        if (!server) {
            errorMessage = "server not running";
            return false;
        }
        QString rootText = rootNodeIdText.trimmed();
        if (rootText.isEmpty()) {
            rootText = "i=85";
        }
        UA_NodeId nodeId;
        QString normalized;
        if (!parseNodeIdString(rootText, nodeId, normalized, &errorMessage)) {
            return false;
        }
        QSet<QString> visited;
        const bool excludeNamespaceZeroChildren = (normalized == "i=85");
        const bool ok = readNodeSnapshot(server,
                                         nodeId,
                                         true,
                                         recurse,
                                         excludeNamespaceZeroChildren,
                                         visited,
                                         node,
                                         errorMessage);
        UA_NodeId_clear(&nodeId);
        return ok;
    }
};

OpcUaServerRuntime::OpcUaServerRuntime()
    : m_impl(new Impl()) {}

OpcUaServerRuntime::~OpcUaServerRuntime() {
    delete m_impl;
}

void OpcUaServerRuntime::setEventCallback(EventCallback callback) {
    m_impl->eventCallback = std::move(callback);
}

bool OpcUaServerRuntime::isRunning() const {
    return m_impl->server != nullptr;
}

QString OpcUaServerRuntime::endpoint() const {
    return m_impl->endpointValue;
}

QString OpcUaServerRuntime::namespaceUri() const {
    return m_impl->namespaceUriValue;
}

int OpcUaServerRuntime::namespaceIndex() const {
    return static_cast<int>(m_impl->namespaceIndexValue);
}

int OpcUaServerRuntime::nodeCount() const {
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);
    return static_cast<int>(m_impl->nodeEntries.size());
}

QString OpcUaServerRuntime::eventMode() const {
    return m_impl->eventModeValue;
}

bool OpcUaServerRuntime::start(const StartOptions& options, QString& errorMessage) {
    return m_impl->start(options, errorMessage);
}

bool OpcUaServerRuntime::stop(int gracefulTimeoutMs, QString& errorMessage) {
    return m_impl->stop(gracefulTimeoutMs, errorMessage);
}

bool OpcUaServerRuntime::upsertNodes(const QJsonArray& nodes,
                                     QJsonArray& results,
                                     QString& errorMessage) {
    return m_impl->upsertNodes(nodes, results, errorMessage);
}

bool OpcUaServerRuntime::deleteNodes(const QStringList& nodeIds,
                                     bool recursive,
                                     QJsonArray& results,
                                     QString& errorMessage) {
    return m_impl->deleteNodes(nodeIds, recursive, results, errorMessage);
}

bool OpcUaServerRuntime::writeValues(const QJsonArray& items,
                                     bool strictType,
                                     QJsonArray& results,
                                     QString& errorMessage) {
    return m_impl->writeValues(items, strictType, results, errorMessage);
}

bool OpcUaServerRuntime::inspectNode(const QString& nodeId,
                                     bool recurse,
                                     QJsonObject& node,
                                     QString& errorMessage) {
    return m_impl->inspectNode(nodeId, recurse, node, errorMessage);
}

bool OpcUaServerRuntime::snapshotNodes(const QString& rootNodeId,
                                       bool recurse,
                                       QJsonObject& node,
                                       QString& errorMessage) {
    return m_impl->snapshotNodes(rootNodeId, recurse, node, errorMessage);
}

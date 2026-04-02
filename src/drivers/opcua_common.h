#pragma once

#include <QJsonValue>
#include <QString>

extern "C" {
#include <open62541/plugin/log.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
}

#include "stdiolink/driver/meta_builder.h"

struct OpcUaVariantStorage {
    OpcUaVariantStorage();
    ~OpcUaVariantStorage();

    OpcUaVariantStorage(const OpcUaVariantStorage&) = delete;
    OpcUaVariantStorage& operator=(const OpcUaVariantStorage&) = delete;

    UA_Variant variant;
};

namespace opcua_common {

UA_Logger* silentLogger();

QString uaStringToQString(const UA_String& value);
QString localizedTextToQString(const UA_LocalizedText& value);
QString qualifiedNameToQString(const UA_QualifiedName& value);
QString guidToQString(const UA_Guid& value);
QString statusCodeText(UA_StatusCode statusCode);
QString formatStatusMessage(const QString& action, UA_StatusCode statusCode);
QString nodeIdToQString(const UA_NodeId& nodeId);
QString expandedNodeIdToQString(const UA_ExpandedNodeId& nodeId);
QString nodeClassToQString(UA_NodeClass nodeClass);
bool isDirectoryNodeClass(UA_NodeClass nodeClass);
bool isBrowsableSnapshotNode(UA_NodeClass nodeClass);
QString jsonValueToText(const QJsonValue& value);
QJsonValue scalarToJson(const void* data, const UA_DataType* type);
QJsonValue variantToJson(const UA_Variant& variant);
bool parseNodeIdString(const QString& input,
                       UA_NodeId& nodeId,
                       QString& normalizedNodeId,
                       QString* errorMessage = nullptr);
QString dataTypeNameFromNodeId(const UA_NodeId& nodeId);
bool resolveBuiltinDataType(const QString& typeName,
                            const UA_DataType*& dataType,
                            UA_NodeId& dataTypeId,
                            QString* errorMessage = nullptr);
bool jsonToVariant(const QString& dataTypeName,
                   const QJsonValue& value,
                   bool strictType,
                   OpcUaVariantStorage& storage,
                   QString* errorMessage = nullptr);
stdiolink::meta::FieldBuilder buildSnapshotNodeField(const QString& name = "node");
void addConnectionParams(stdiolink::meta::CommandBuilder& command);

} // namespace opcua_common

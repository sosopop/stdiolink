#include "opcua_common.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUuid>

using namespace stdiolink::meta;

namespace {

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

QString trimLower(const QString& value) {
    return value.trimmed().toLower();
}

bool parseIntegerString(const QString& text,
                        qint64 minValue,
                        qint64 maxValue,
                        qint64& value) {
    bool ok = false;
    const qint64 parsed = text.trimmed().toLongLong(&ok, 10);
    if (!ok || parsed < minValue || parsed > maxValue) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseUnsignedString(const QString& text,
                         quint64 maxValue,
                         quint64& value) {
    bool ok = false;
    const quint64 parsed = text.trimmed().toULongLong(&ok, 10);
    if (!ok || parsed > maxValue) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseIntegerValue(const QJsonValue& value,
                       bool strictType,
                       qint64 minValue,
                       qint64 maxValue,
                       qint64& outValue,
                       QString* errorMessage) {
    if (value.isDouble()) {
        const double raw = value.toDouble();
        const qint64 asInt = static_cast<qint64>(raw);
        if (raw != static_cast<double>(asInt) || asInt < minValue || asInt > maxValue) {
            if (errorMessage) {
                *errorMessage = QString("value must be an integer in [%1, %2]")
                                    .arg(minValue)
                                    .arg(maxValue);
            }
            return false;
        }
        outValue = asInt;
        return true;
    }

    if (!strictType && value.isString()) {
        if (parseIntegerString(value.toString(), minValue, maxValue, outValue)) {
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = strictType
            ? QString("value must be a JSON integer in [%1, %2]").arg(minValue).arg(maxValue)
            : QString("value must be an integer or integer string in [%1, %2]")
                  .arg(minValue)
                  .arg(maxValue);
    }
    return false;
}

bool parseUnsignedValue(const QJsonValue& value,
                        bool strictType,
                        quint64 maxValue,
                        quint64& outValue,
                        QString* errorMessage) {
    if (value.isDouble()) {
        const double raw = value.toDouble();
        const quint64 asInt = static_cast<quint64>(raw);
        if (raw != static_cast<double>(asInt) || asInt > maxValue) {
            if (errorMessage) {
                *errorMessage = QString("value must be an unsigned integer in [0, %1]")
                                    .arg(maxValue);
            }
            return false;
        }
        outValue = asInt;
        return true;
    }

    if (!strictType && value.isString()) {
        if (parseUnsignedString(value.toString(), maxValue, outValue)) {
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = strictType
            ? QString("value must be a JSON unsigned integer in [0, %1]").arg(maxValue)
            : QString("value must be an unsigned integer or decimal string in [0, %1]")
                  .arg(maxValue);
    }
    return false;
}

bool parseBoolValue(const QJsonValue& value,
                    bool strictType,
                    bool& outValue,
                    QString* errorMessage) {
    if (value.isBool()) {
        outValue = value.toBool();
        return true;
    }

    if (!strictType) {
        if (value.isDouble()) {
            const double raw = value.toDouble();
            if (raw == 0.0) {
                outValue = false;
                return true;
            }
            if (raw == 1.0) {
                outValue = true;
                return true;
            }
        }
        if (value.isString()) {
            const QString key = trimLower(value.toString());
            if (key == "true" || key == "1") {
                outValue = true;
                return true;
            }
            if (key == "false" || key == "0") {
                outValue = false;
                return true;
            }
        }
    }

    if (errorMessage) {
        *errorMessage = strictType
            ? "value must be a JSON boolean"
            : "value must be a boolean, 0/1, or true/false string";
    }
    return false;
}

bool parseDoubleValue(const QJsonValue& value,
                      bool strictType,
                      double& outValue,
                      QString* errorMessage) {
    if (value.isDouble()) {
        outValue = value.toDouble();
        return true;
    }

    if (!strictType && value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            outValue = parsed;
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = strictType ? "value must be a JSON number"
                                   : "value must be a number or numeric string";
    }
    return false;
}

QDateTime parseIsoDateTime(const QString& text) {
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    return parsed.toUTC();
}

QDateTime uaDateTimeToQDateTime(UA_DateTime value) {
    const qint64 msecsSinceUnixEpoch =
        (static_cast<qint64>(value) - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC;
    return QDateTime::fromMSecsSinceEpoch(msecsSinceUnixEpoch, QTimeZone::UTC);
}

bool buildScalarVariant(const void* scalar,
                        const UA_DataType* dataType,
                        OpcUaVariantStorage& storage,
                        QString* errorMessage) {
    UA_Variant_clear(&storage.variant);
    const UA_StatusCode statusCode =
        UA_Variant_setScalarCopy(&storage.variant, scalar, dataType);
    if (statusCode != UA_STATUSCODE_GOOD) {
        if (errorMessage) {
            *errorMessage = opcua_common::formatStatusMessage(
                "failed to build UA_Variant",
                statusCode);
        }
        return false;
    }
    return true;
}

} // namespace

OpcUaVariantStorage::OpcUaVariantStorage() {
    UA_Variant_init(&variant);
}

OpcUaVariantStorage::~OpcUaVariantStorage() {
    UA_Variant_clear(&variant);
}

namespace opcua_common {

UA_Logger* silentLogger() {
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
    return QString("%1:%2")
        .arg(value.namespaceIndex)
        .arg(uaStringToQString(value.name));
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
        return uaDateTimeToQDateTime(*static_cast<const UA_DateTime*>(data))
            .toString(Qt::ISODateWithMs);
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

QString dataTypeNameFromNodeId(const UA_NodeId& nodeId) {
    for (size_t i = 0; i < UA_TYPES_COUNT; ++i) {
        if (UA_NodeId_equal(&UA_TYPES[i].typeId, &nodeId)) {
            return QString::fromUtf8(UA_TYPES[i].typeName);
        }
    }
    return nodeIdToQString(nodeId);
}

bool resolveBuiltinDataType(const QString& typeName,
                            const UA_DataType*& dataType,
                            UA_NodeId& dataTypeId,
                            QString* errorMessage) {
    const QString key = trimLower(typeName);
    int typeIndex = -1;

    if (key == "bool" || key == "boolean") {
        typeIndex = UA_TYPES_BOOLEAN;
    } else if (key == "int16") {
        typeIndex = UA_TYPES_INT16;
    } else if (key == "uint16") {
        typeIndex = UA_TYPES_UINT16;
    } else if (key == "int32") {
        typeIndex = UA_TYPES_INT32;
    } else if (key == "uint32") {
        typeIndex = UA_TYPES_UINT32;
    } else if (key == "int64") {
        typeIndex = UA_TYPES_INT64;
    } else if (key == "uint64") {
        typeIndex = UA_TYPES_UINT64;
    } else if (key == "float") {
        typeIndex = UA_TYPES_FLOAT;
    } else if (key == "double") {
        typeIndex = UA_TYPES_DOUBLE;
    } else if (key == "string") {
        typeIndex = UA_TYPES_STRING;
    } else if (key == "bytestring") {
        typeIndex = UA_TYPES_BYTESTRING;
    } else if (key == "datetime") {
        typeIndex = UA_TYPES_DATETIME;
    }

    if (typeIndex < 0) {
        if (errorMessage) {
            *errorMessage = QString("Unsupported data_type: %1").arg(typeName);
        }
        return false;
    }

    dataType = &UA_TYPES[typeIndex];
    dataTypeId = dataType->typeId;
    return true;
}

bool jsonToVariant(const QString& dataTypeName,
                   const QJsonValue& value,
                   bool strictType,
                   OpcUaVariantStorage& storage,
                   QString* errorMessage) {
    const UA_DataType* dataType = nullptr;
    UA_NodeId dataTypeId = UA_NODEID_NULL;
    if (!resolveBuiltinDataType(dataTypeName, dataType, dataTypeId, errorMessage)) {
        return false;
    }

    if (dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        bool parsed = false;
        if (!parseBoolValue(value, strictType, parsed, errorMessage)) {
            return false;
        }
        UA_Boolean uaValue = parsed ? UA_TRUE : UA_FALSE;
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_INT16]) {
        qint64 parsed = 0;
        if (!parseIntegerValue(value, strictType, std::numeric_limits<qint16>::min(),
                               std::numeric_limits<qint16>::max(), parsed, errorMessage)) {
            return false;
        }
        const UA_Int16 uaValue = static_cast<UA_Int16>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_UINT16]) {
        quint64 parsed = 0;
        if (!parseUnsignedValue(value, strictType, std::numeric_limits<quint16>::max(),
                                parsed, errorMessage)) {
            return false;
        }
        const UA_UInt16 uaValue = static_cast<UA_UInt16>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_INT32]) {
        qint64 parsed = 0;
        if (!parseIntegerValue(value, strictType, std::numeric_limits<qint32>::min(),
                               std::numeric_limits<qint32>::max(), parsed, errorMessage)) {
            return false;
        }
        const UA_Int32 uaValue = static_cast<UA_Int32>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_UINT32]) {
        quint64 parsed = 0;
        if (!parseUnsignedValue(value, strictType, std::numeric_limits<quint32>::max(),
                                parsed, errorMessage)) {
            return false;
        }
        const UA_UInt32 uaValue = static_cast<UA_UInt32>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_INT64]) {
        qint64 parsed = 0;
        if (!value.isString() && strictType) {
            if (errorMessage) {
                *errorMessage = "int64 value must be a decimal string";
            }
            return false;
        }
        const QString text = value.isString() ? value.toString() : jsonValueToText(value);
        if (!parseIntegerString(text,
                                std::numeric_limits<qint64>::min(),
                                std::numeric_limits<qint64>::max(),
                                parsed)) {
            if (errorMessage) {
                *errorMessage = "int64 value must be a valid decimal string";
            }
            return false;
        }
        const UA_Int64 uaValue = static_cast<UA_Int64>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_UINT64]) {
        quint64 parsed = 0;
        if (!value.isString() && strictType) {
            if (errorMessage) {
                *errorMessage = "uint64 value must be a decimal string";
            }
            return false;
        }
        const QString text = value.isString() ? value.toString() : jsonValueToText(value);
        if (!parseUnsignedString(text, std::numeric_limits<quint64>::max(), parsed)) {
            if (errorMessage) {
                *errorMessage = "uint64 value must be a valid decimal string";
            }
            return false;
        }
        const UA_UInt64 uaValue = static_cast<UA_UInt64>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
        double parsed = 0.0;
        if (!parseDoubleValue(value, strictType, parsed, errorMessage)) {
            return false;
        }
        const UA_Float uaValue = static_cast<UA_Float>(parsed);
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
        double parsed = 0.0;
        if (!parseDoubleValue(value, strictType, parsed, errorMessage)) {
            return false;
        }
        const UA_Double uaValue = parsed;
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_STRING]) {
        if (!value.isString()) {
            if (errorMessage) {
                *errorMessage = "string value must be a JSON string";
            }
            return false;
        }
        const QByteArray utf8 = value.toString().toUtf8();
        UA_String uaValue = UA_STRING(const_cast<char*>(utf8.constData()));
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_BYTESTRING]) {
        if (!value.isString()) {
            if (errorMessage) {
                *errorMessage = "bytestring value must be a base64 string";
            }
            return false;
        }
        const QByteArray raw = QByteArray::fromBase64(value.toString().toUtf8());
        if (raw.isEmpty() && !value.toString().isEmpty()) {
            if (errorMessage) {
                *errorMessage = "bytestring value must be valid base64";
            }
            return false;
        }
        UA_ByteString uaValue;
        uaValue.length = static_cast<size_t>(raw.size());
        uaValue.data = reinterpret_cast<UA_Byte*>(const_cast<char*>(raw.constData()));
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (dataType == &UA_TYPES[UA_TYPES_DATETIME]) {
        if (!value.isString()) {
            if (errorMessage) {
                *errorMessage = "datetime value must be an ISO8601 string";
            }
            return false;
        }
        const QDateTime parsed = parseIsoDateTime(value.toString());
        if (!parsed.isValid()) {
            if (errorMessage) {
                *errorMessage = "datetime value must be a valid ISO8601 string";
            }
            return false;
        }
        const UA_DateTime uaValue =
            UA_DATETIME_UNIX_EPOCH + parsed.toMSecsSinceEpoch() * UA_DATETIME_MSEC;
        return buildScalarVariant(&uaValue, dataType, storage, errorMessage);
    }

    if (errorMessage) {
        *errorMessage = QString("Unsupported data_type: %1").arg(dataTypeName);
    }
    return false;
}

FieldBuilder buildSnapshotNodeField(const QString& name) {
    FieldBuilder nodeField(name, FieldType::Object);
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
    return nodeField;
}

void addConnectionParams(CommandBuilder& command) {
    command
        .param(FieldBuilder("host", FieldType::String)
            .required()
            .description(QString::fromUtf8("OPC UA 服务器地址，如 127.0.0.1"))
            .placeholder("127.0.0.1"))
        .param(FieldBuilder("port", FieldType::Int)
            .defaultValue(4840)
            .range(1, 65535)
            .description(QString::fromUtf8("OPC UA 服务器端口，默认 4840")))
        .param(FieldBuilder("timeout_ms", FieldType::Int)
            .defaultValue(3000)
            .range(1, 30000)
            .description(QString::fromUtf8("连接与读写超时(ms)，默认 3000")));
}

} // namespace opcua_common

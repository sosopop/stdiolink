#include "cli_schema_parser.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "console_args.h"

namespace stdiolink {

namespace {

QString canonicalLiteral(const QJsonValue& value) {
    QJsonArray wrapper;
    wrapper.append(value);
    QString json = QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
    if (json.size() >= 2 && json.front() == '[' && json.back() == ']') {
        return json.mid(1, json.size() - 2);
    }
    return json;
}

bool parseJsonLiteral(const QString& text, QJsonValue& out) {
    QJsonParseError error{};
    const QByteArray wrapped = "[" + text.toUtf8() + "]";
    const QJsonDocument doc = QJsonDocument::fromJson(wrapped, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return false;
    }
    const QJsonArray arr = doc.array();
    if (arr.size() != 1) {
        return false;
    }
    out = arr.at(0);
    return true;
}

const meta::FieldMeta* findFieldByName(const QVector<meta::FieldMeta>& fields, const QString& name) {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

const meta::FieldMeta* resolveFieldMetaByPath(const QVector<meta::FieldMeta>& rootFields,
                                              const CliPath& path,
                                              QString* error) {
    if (path.isEmpty() || path.first().kind != CliPathSegment::Kind::Key) {
        return nullptr;
    }

    const meta::FieldMeta* field = findFieldByName(rootFields, path.first().key);
    if (field == nullptr) {
        return nullptr;
    }

    for (int i = 1; i < path.size(); ++i) {
        const CliPathSegment& segment = path.at(i);
        if (field->type == meta::FieldType::Array) {
            if (segment.kind != CliPathSegment::Kind::Index &&
                segment.kind != CliPathSegment::Kind::Append) {
                if (error != nullptr) {
                    *error = QString("path does not match array field: %1").arg(field->name);
                }
                return nullptr;
            }
            field = field->items.get();
            if (field == nullptr) {
                return nullptr;
            }
            continue;
        }

        if (field->type == meta::FieldType::Object) {
            if (segment.kind != CliPathSegment::Kind::Key) {
                if (error != nullptr) {
                    *error = QString("path does not match object field: %1").arg(field->name);
                }
                return nullptr;
            }
            field = findFieldByName(field->fields, segment.key);
            if (field == nullptr) {
                return nullptr;
            }
            continue;
        }

        if (error != nullptr) {
            *error = QString("path exceeds scalar field: %1").arg(field->name);
        }
        return nullptr;
    }

    return field;
}

QJsonValue decodeStringOrKeepRaw(const QString& raw) {
    QJsonValue parsed;
    if (parseJsonLiteral(raw, parsed) && parsed.isString()) {
        return parsed;
    }
    return QJsonValue(raw);
}

bool parseBoolValue(const QString& raw, QJsonValue& out, QString* error) {
    if (raw == "true") {
        out = true;
        return true;
    }
    if (raw == "false") {
        out = false;
        return true;
    }
    if (error != nullptr) {
        *error = "expected boolean literal";
    }
    return false;
}

bool parseIntValue(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const int value = raw.toInt(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected integer literal";
        }
        return false;
    }
    out = value;
    return true;
}

bool parseSafeInt64Value(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const qlonglong value = raw.toLongLong(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected integer literal";
        }
        return false;
    }
    static constexpr qlonglong kMinSafe = -9007199254740992LL;
    static constexpr qlonglong kMaxSafe = 9007199254740992LL;
    if (value < kMinSafe || value > kMaxSafe) {
        if (error != nullptr) {
            *error = "integer out of safe range";
        }
        return false;
    }
    out = static_cast<double>(value);
    return true;
}

bool parseDoubleValue(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const double value = raw.toDouble(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected number literal";
        }
        return false;
    }
    out = value;
    return true;
}

bool parseExpectedContainerValue(const QString& raw,
                                 QJsonValue::Type expectedType,
                                 QJsonValue& out,
                                 QString* error) {
    QJsonValue parsed;
    if (!parseJsonLiteral(raw, parsed) || parsed.type() != expectedType) {
        if (error != nullptr) {
            *error = (expectedType == QJsonValue::Array) ? "expected array literal"
                                                         : "expected object literal";
        }
        return false;
    }
    out = parsed;
    return true;
}

bool resolveFieldValue(const QString& raw,
                       const meta::FieldMeta* fieldMeta,
                       QJsonValue& out,
                       QString* error) {
    if (fieldMeta == nullptr) {
        out = inferType(raw);
        return true;
    }

    switch (fieldMeta->type) {
    case meta::FieldType::String:
    case meta::FieldType::Enum:
        out = decodeStringOrKeepRaw(raw);
        return true;
    case meta::FieldType::Bool:
        return parseBoolValue(raw, out, error);
    case meta::FieldType::Int:
        return parseIntValue(raw, out, error);
    case meta::FieldType::Int64:
        return parseSafeInt64Value(raw, out, error);
    case meta::FieldType::Double:
        return parseDoubleValue(raw, out, error);
    case meta::FieldType::Object:
        return parseExpectedContainerValue(raw, QJsonValue::Object, out, error);
    case meta::FieldType::Array:
        return parseExpectedContainerValue(raw, QJsonValue::Array, out, error);
    case meta::FieldType::Any:
        out = inferType(raw);
        return true;
    }
    return false;
}

} // namespace

bool CliSchemaParser::parseArgs(const QList<RawCliArg>& rawArgs,
                                const QVector<meta::FieldMeta>& rootFields,
                                QJsonObject& out,
                                QString* error,
                                int* errorArgIndex) {
    QList<RawCliArg> typedArgs;
    typedArgs.reserve(rawArgs.size());

    for (int i = 0; i < rawArgs.size(); ++i) {
        const auto& rawArg = rawArgs.at(i);

        CliPath path;
        QString pathError;
        if (!JsonCliCodec::parsePath(rawArg.path, path, &pathError)) {
            if (error != nullptr) {
                *error = pathError;
            }
            if (errorArgIndex != nullptr) {
                *errorArgIndex = i;
            }
            return false;
        }

        QString metaError;
        const meta::FieldMeta* fieldMeta = resolveFieldMetaByPath(rootFields, path, &metaError);
        if (!metaError.isEmpty()) {
            if (error != nullptr) {
                *error = metaError;
            }
            if (errorArgIndex != nullptr) {
                *errorArgIndex = i;
            }
            return false;
        }

        QJsonValue value;
        QString valueError;
        if (!resolveFieldValue(rawArg.rawValue, fieldMeta, value, &valueError)) {
            if (error != nullptr) {
                *error = QString("%1: %2").arg(rawArg.path, valueError);
            }
            if (errorArgIndex != nullptr) {
                *errorArgIndex = i;
            }
            return false;
        }

        typedArgs.append(RawCliArg{rawArg.path, canonicalLiteral(value)});
    }

    const bool ok = JsonCliCodec::parseArgs(
        typedArgs,
        CliParseOptions{CliValueMode::Canonical},
        out,
        error);
    if (!ok && errorArgIndex != nullptr) {
        *errorArgIndex = rawArgs.isEmpty() ? -1 : (rawArgs.size() - 1);
    }
    return ok;
}

} // namespace stdiolink

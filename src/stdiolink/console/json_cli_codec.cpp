#include "json_cli_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>

namespace stdiolink {

namespace {

struct CliNode {
    enum class Kind {
        Unset,
        Scalar,
        Object,
        Array
    };

    enum class Origin {
        None,
        Scalar,
        LiteralContainer,
        Aggregated
    };

    Kind kind = Kind::Unset;
    Origin origin = Origin::None;
    QJsonValue scalar;
    QMap<QString, CliNode> objectChildren;
    QVector<CliNode> arrayChildren;
    bool hasAppendWrites = false;
    bool hasExplicitIndexWrites = false;
};

QString quotedForPath(const QString& key) {
    QString escaped = key;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    return "[\"" + escaped + "\"]";
}

bool isSimpleKey(const QString& key) {
    static const QRegularExpression kPattern("^[A-Za-z_][A-Za-z0-9_-]*$");
    return kPattern.match(key).hasMatch();
}

QString renderPath(const CliPath& path) {
    QString out;
    for (const auto& segment : path) {
        switch (segment.kind) {
        case CliPathSegment::Kind::Key:
            if (out.isEmpty()) {
                out += isSimpleKey(segment.key) ? segment.key : quotedForPath(segment.key);
            } else if (isSimpleKey(segment.key)) {
                out += "." + segment.key;
            } else {
                out += quotedForPath(segment.key);
            }
            break;
        case CliPathSegment::Kind::Index:
            out += QString("[%1]").arg(segment.index);
            break;
        case CliPathSegment::Kind::Append:
            out += "[]";
            break;
        }
    }
    return out;
}

QString canonicalLiteral(const QJsonValue& value) {
    if (value.isNull() || value.isUndefined()) {
        return "null";
    }
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

QJsonValue inferFriendlyValue(const QString& text) {
    QJsonValue parsed;
    if (parseJsonLiteral(text, parsed)) {
        return parsed;
    }
    return QJsonValue(text);
}

bool ensureObjectNode(CliNode& node, QString* error, bool childPath) {
    if (node.kind == CliNode::Kind::Unset) {
        node.kind = CliNode::Kind::Object;
        node.origin = CliNode::Origin::Aggregated;
        return true;
    }
    if (node.kind == CliNode::Kind::Object) {
        if (childPath && node.origin == CliNode::Origin::LiteralContainer) {
            if (error != nullptr) {
                *error = "path conflict: container literal vs child path";
            }
            return false;
        }
        return true;
    }
    if (error != nullptr) {
        *error = "path conflict: scalar vs object";
    }
    return false;
}

bool ensureArrayNode(CliNode& node, QString* error, bool childPath) {
    if (node.kind == CliNode::Kind::Unset) {
        node.kind = CliNode::Kind::Array;
        node.origin = CliNode::Origin::Aggregated;
        return true;
    }
    if (node.kind == CliNode::Kind::Array) {
        if (childPath && node.origin == CliNode::Origin::LiteralContainer) {
            if (error != nullptr) {
                *error = "path conflict: container literal vs child path";
            }
            return false;
        }
        return true;
    }
    if (error != nullptr) {
        *error = "path conflict: scalar vs object";
    }
    return false;
}

bool assignPath(CliNode& node,
                const CliPath& path,
                int segmentIndex,
                const QJsonValue& value,
                QString* error) {
    if (segmentIndex >= path.size()) {
        const bool isContainer = value.isObject() || value.isArray();
        if (node.kind == CliNode::Kind::Object || node.kind == CliNode::Kind::Array) {
            if (node.origin == CliNode::Origin::Aggregated) {
                if (error != nullptr) {
                    *error = "path conflict: container literal vs child path";
                }
                return false;
            }
        }
        node.kind = isContainer ? (value.isObject() ? CliNode::Kind::Object : CliNode::Kind::Array)
                                : CliNode::Kind::Scalar;
        node.origin = isContainer ? CliNode::Origin::LiteralContainer : CliNode::Origin::Scalar;
        node.scalar = value;
        node.objectChildren.clear();
        node.arrayChildren.clear();
        node.hasAppendWrites = false;
        node.hasExplicitIndexWrites = false;
        return true;
    }

    const CliPathSegment& segment = path.at(segmentIndex);
    const bool isLeaf = (segmentIndex == path.size() - 1);

    if (segment.kind == CliPathSegment::Kind::Key) {
        if (!ensureObjectNode(node, error, true)) {
            return false;
        }
        CliNode& child = node.objectChildren[segment.key];
        return assignPath(child, path, segmentIndex + 1, value, error);
    }

    if (segment.kind == CliPathSegment::Kind::Index) {
        if (!ensureArrayNode(node, error, true)) {
            return false;
        }
        if (node.hasAppendWrites) {
            if (error != nullptr) {
                *error = "path conflict: append vs explicit index";
            }
            return false;
        }
        node.hasExplicitIndexWrites = true;
        if (segment.index < 0) {
            if (error != nullptr) {
                *error = "invalid array index";
            }
            return false;
        }
        while (node.arrayChildren.size() <= segment.index) {
            node.arrayChildren.append(CliNode{});
        }
        return assignPath(node.arrayChildren[segment.index], path, segmentIndex + 1, value, error);
    }

    if (!isLeaf) {
        if (error != nullptr) {
            *error = "append path must be terminal";
        }
        return false;
    }

    if (!ensureArrayNode(node, error, true)) {
        return false;
    }
    if (node.hasExplicitIndexWrites) {
        if (error != nullptr) {
            *error = "path conflict: append vs explicit index";
        }
        return false;
    }
    node.hasAppendWrites = true;
    CliNode child;
    if (!assignPath(child, path, segmentIndex + 1, value, error)) {
        return false;
    }
    node.arrayChildren.append(child);
    return true;
}

QJsonValue nodeToJson(const CliNode& node) {
    switch (node.kind) {
    case CliNode::Kind::Unset:
        return QJsonValue();
    case CliNode::Kind::Scalar:
        return node.scalar;
    case CliNode::Kind::Object:
        if (node.origin == CliNode::Origin::LiteralContainer && node.scalar.isObject()) {
            return node.scalar;
        }
        {
            QJsonObject obj;
            for (auto it = node.objectChildren.begin(); it != node.objectChildren.end(); ++it) {
                obj.insert(it.key(), nodeToJson(it.value()));
            }
            return obj;
        }
    case CliNode::Kind::Array:
        if (node.origin == CliNode::Origin::LiteralContainer && node.scalar.isArray()) {
            return node.scalar;
        }
        {
            QJsonArray arr;
            for (const auto& child : node.arrayChildren) {
                arr.append(nodeToJson(child));
            }
            return arr;
        }
    }
    return QJsonValue();
}

void renderValueRecursive(const CliPath& prefix,
                          const QJsonValue& value,
                          QStringList& out,
                          const CliRenderOptions& opts) {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        if (obj.isEmpty()) {
            out.append("--" + renderPath(prefix) +
                       (opts.useEquals ? "=" : " ") + "{}");
            return;
        }
        QStringList keys = obj.keys();
        keys.sort(Qt::CaseSensitive);
        for (const QString& key : keys) {
            CliPath next = prefix;
            next.append(CliPathSegment{CliPathSegment::Kind::Key, key, -1});
            renderValueRecursive(next, obj.value(key), out, opts);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        if (arr.isEmpty()) {
            out.append("--" + renderPath(prefix) +
                       (opts.useEquals ? "=" : " ") + "[]");
            return;
        }
        for (int i = 0; i < arr.size(); ++i) {
            CliPath next = prefix;
            next.append(CliPathSegment{CliPathSegment::Kind::Index, QString(), i});
            renderValueRecursive(next, arr.at(i), out, opts);
        }
        return;
    }

    const QString path = "--" + renderPath(prefix);
    const QString literal = canonicalLiteral(value);
    out.append(opts.useEquals ? (path + "=" + literal) : QStringList{path, literal}.join(' '));
}

} // namespace

bool JsonCliCodec::parsePath(const QString& path, CliPath& out, QString* error) {
    out.clear();
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = "invalid path syntax: empty path";
        }
        return false;
    }

    int i = 0;
    auto fail = [&](const QString& msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };

    auto parseBareKey = [&](QString& key) {
        const int start = i;
        while (i < path.size() && path.at(i) != '.' && path.at(i) != '[') {
            ++i;
        }
        key = path.mid(start, i - start);
        return !key.isEmpty();
    };

    while (i < path.size()) {
        if (path.at(i) == '.') {
            ++i;
            if (i >= path.size()) {
                return fail("invalid path syntax: trailing dot");
            }
        }

        if (path.at(i) == '[') {
            if (i + 1 >= path.size()) {
                return fail("invalid path syntax: unterminated bracket");
            }
            if (path.at(i + 1) == ']') {
                out.append(CliPathSegment{CliPathSegment::Kind::Append, QString(), -1});
                i += 2;
                continue;
            }
            if (path.at(i + 1) == '"') {
                i += 2;
                QString key;
                while (i < path.size()) {
                    const QChar ch = path.at(i);
                    if (ch == '\\') {
                        if (i + 1 >= path.size()) {
                            return fail("invalid path syntax: bad escape");
                        }
                        key.append(path.at(i + 1));
                        i += 2;
                        continue;
                    }
                    if (ch == '"') {
                        break;
                    }
                    key.append(ch);
                    ++i;
                }
                if (i >= path.size() || path.at(i) != '"' || i + 1 >= path.size() || path.at(i + 1) != ']') {
                    return fail("invalid path syntax: unterminated quoted key");
                }
                out.append(CliPathSegment{CliPathSegment::Kind::Key, key, -1});
                i += 2;
                continue;
            }

            ++i;
            const int start = i;
            while (i < path.size() && path.at(i) != ']') {
                ++i;
            }
            if (i >= path.size()) {
                return fail("invalid path syntax: unterminated index");
            }
            const QString token = path.mid(start, i - start);
            bool ok = false;
            const int index = token.toInt(&ok);
            if (!ok) {
                return fail("invalid array index");
            }
            out.append(CliPathSegment{CliPathSegment::Kind::Index, QString(), index});
            ++i;
            continue;
        }

        QString key;
        if (!parseBareKey(key)) {
            return fail("invalid path syntax: empty segment");
        }
        out.append(CliPathSegment{CliPathSegment::Kind::Key, key, -1});
    }

    if (out.isEmpty()) {
        return fail("invalid path syntax: empty path");
    }
    return true;
}

bool JsonCliCodec::parseArgs(const QList<RawCliArg>& args,
                             const CliParseOptions& opts,
                             QJsonObject& out,
                             QString* error) {
    CliNode root;
    root.kind = CliNode::Kind::Object;
    root.origin = CliNode::Origin::Aggregated;

    for (const auto& arg : args) {
        CliPath path;
        QString pathError;
        if (!parsePath(arg.path, path, &pathError)) {
            if (error != nullptr) {
                *error = pathError;
            }
            return false;
        }

        QJsonValue value;
        if (opts.mode == CliValueMode::Canonical) {
            if (!parseJsonLiteral(arg.rawValue, value)) {
                if (error != nullptr) {
                    *error = "invalid canonical JSON literal";
                }
                return false;
            }
        } else {
            value = inferFriendlyValue(arg.rawValue);
        }

        QString assignError;
        if (!assignPath(root, path, 0, value, &assignError)) {
            if (error != nullptr) {
                *error = assignError;
            }
            return false;
        }
    }

    out = nodeToJson(root).toObject();
    return true;
}

QStringList JsonCliCodec::renderArgs(const QJsonObject& data,
                                     const CliRenderOptions& opts) {
    QStringList out;
    QStringList keys = data.keys();
    keys.sort(Qt::CaseSensitive);
    for (const QString& key : keys) {
        CliPath prefix;
        prefix.append(CliPathSegment{CliPathSegment::Kind::Key, key, -1});
        renderValueRecursive(prefix, data.value(key), out, opts);
    }
    return out;
}

} // namespace stdiolink

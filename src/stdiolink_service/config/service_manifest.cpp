#include "service_manifest.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace stdiolink_service {

static const QSet<QString> s_knownFields = {
    "manifestVersion", "id", "name", "version", "description", "author"
};

ServiceManifest ServiceManifest::fromJson(const QJsonObject& obj, QString& error) {
    ServiceManifest m;

    // Reject unknown fields
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!s_knownFields.contains(it.key())) {
            error = QString("unknown field in manifest.json: \"%1\"").arg(it.key());
            return m;
        }
    }

    // manifestVersion
    if (!obj.contains("manifestVersion")) {
        error = "missing required field: manifestVersion";
        return m;
    }
    m.manifestVersion = obj.value("manifestVersion").toString();
    if (m.manifestVersion != "1") {
        error = QString("unsupported manifestVersion: \"%1\" (expected \"1\")").arg(m.manifestVersion);
        return m;
    }

    // id
    if (!obj.contains("id") || obj.value("id").toString().isEmpty()) {
        error = "missing required field: id";
        return m;
    }
    m.id = obj.value("id").toString();

    // name
    if (!obj.contains("name") || obj.value("name").toString().isEmpty()) {
        error = "missing required field: name";
        return m;
    }
    m.name = obj.value("name").toString();

    // version
    if (!obj.contains("version") || obj.value("version").toString().isEmpty()) {
        error = "missing required field: version";
        return m;
    }
    m.version = obj.value("version").toString();

    // optional fields
    m.description = obj.value("description").toString();
    m.author = obj.value("author").toString();

    error.clear();
    return m;
}

ServiceManifest ServiceManifest::loadFromFile(const QString& filePath, QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open manifest file: %1").arg(filePath);
        return {};
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QString("manifest.json parse error: %1").arg(parseError.errorString());
        return {};
    }

    if (!doc.isObject()) {
        error = "manifest.json must contain a JSON object";
        return {};
    }

    return fromJson(doc.object(), error);
}

bool ServiceManifest::isValid(QString& error) const {
    if (manifestVersion != "1") {
        error = "invalid manifestVersion";
        return false;
    }
    if (id.isEmpty()) {
        error = "id is empty";
        return false;
    }
    if (name.isEmpty()) {
        error = "name is empty";
        return false;
    }
    if (version.isEmpty()) {
        error = "version is empty";
        return false;
    }
    return true;
}

} // namespace stdiolink_service

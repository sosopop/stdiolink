#include "meta_exporter.h"
#include <QFile>
#include <QJsonDocument>

namespace stdiolink {

QByteArray MetaExporter::exportJson(const meta::DriverMeta& meta, bool pretty) {
    QJsonObject json = meta.toJson();
    QJsonDocument doc(json);
    return doc.toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact);
}

bool MetaExporter::exportToFile(const meta::DriverMeta& meta, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QByteArray data = exportJson(meta, true);
    qint64 written = file.write(data);
    file.close();
    return written == data.size();
}

} // namespace stdiolink

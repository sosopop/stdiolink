#pragma once

#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace stdiolink_server {

inline QHttpServerResponse jsonResponse(
    const QJsonObject& obj,
    QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok) {
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(obj).toJson(QJsonDocument::Compact),
        code);
}

inline QHttpServerResponse jsonResponse(
    const QJsonArray& arr,
    QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok) {
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(arr).toJson(QJsonDocument::Compact),
        code);
}

inline QHttpServerResponse errorResponse(QHttpServerResponse::StatusCode code,
                                         const QString& message) {
    return jsonResponse(QJsonObject{{"error", message}}, code);
}

inline QHttpServerResponse noContentResponse() {
    return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
}

} // namespace stdiolink_server

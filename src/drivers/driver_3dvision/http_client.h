#pragma once

#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>

class HttpClient : public QObject {
    Q_OBJECT
public:
    explicit HttpClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url);
    QString baseUrl() const { return m_baseUrl; }

    void setToken(const QString& token);
    QString token() const { return m_token; }
    void clearToken() { m_token.clear(); }

    // Synchronous POST request (JSON body)
    QJsonObject post(const QString& path, const QJsonObject& data,
                     int timeoutMs = 30000);

    // Synchronous POST request (binary body, for file upload)
    QJsonObject postBinary(const QString& path, const QByteArray& data,
                           const QString& queryParams = QString(),
                           int timeoutMs = 60000);

private:
    QNetworkRequest createRequest(const QString& path,
                                  const QString& queryParams = QString());
    QJsonObject processReply(QNetworkReply* reply, int timeoutMs);

    QNetworkAccessManager* m_manager;
    QString m_baseUrl;
    QString m_token;
};

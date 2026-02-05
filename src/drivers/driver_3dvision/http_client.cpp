#include "http_client.h"

#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

HttpClient::HttpClient(QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_baseUrl("http://localhost:6100")
{
}

void HttpClient::setBaseUrl(const QString& url)
{
    m_baseUrl = url;
    if (m_baseUrl.endsWith('/')) {
        m_baseUrl.chop(1);
    }
}

void HttpClient::setToken(const QString& token)
{
    m_token = token;
}

QNetworkRequest HttpClient::createRequest(const QString& path,
                                          const QString& queryParams)
{
    QString fullUrl = m_baseUrl + path;
    if (!queryParams.isEmpty()) {
        fullUrl += "?" + queryParams;
    }

    QNetworkRequest request{QUrl(fullUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (!m_token.isEmpty()) {
        request.setRawHeader("token", m_token.toUtf8());
    }

    return request;
}

QJsonObject HttpClient::processReply(QNetworkReply* reply, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    QJsonObject result;

    if (timer.isActive()) {
        timer.stop();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                result = doc.object();
            } else {
                result["code"] = -1;
                result["message"] = "Invalid JSON response";
            }
        } else {
            result["code"] = -1;
            result["message"] = reply->errorString();
        }
    } else {
        reply->abort();
        result["code"] = -1;
        result["message"] = "Request timeout";
    }

    reply->deleteLater();
    return result;
}

QJsonObject HttpClient::post(const QString& path, const QJsonObject& data,
                             int timeoutMs)
{
    QNetworkRequest request = createRequest(path);
    QByteArray body = QJsonDocument(data).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_manager->post(request, body);
    return processReply(reply, timeoutMs);
}

QJsonObject HttpClient::postBinary(const QString& path, const QByteArray& data,
                                   const QString& queryParams, int timeoutMs)
{
    QNetworkRequest request = createRequest(path, queryParams);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/octet-stream");
    QNetworkReply* reply = m_manager->post(request, data);
    return processReply(reply, timeoutMs);
}

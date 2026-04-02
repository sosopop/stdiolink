#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

class OpcUaServerRuntime {
public:
    struct StartOptions {
        QString bindHost = "0.0.0.0";
        quint16 listenPort = 4840;
        QString endpointPath;
        QString serverName = "stdiolink OPC UA Server";
        QString applicationUri = "urn:stdiolink:opcua:server";
        QString namespaceUri = "urn:stdiolink:opcua:nodes";
        QString eventMode = "write";
    };

    using EventCallback = std::function<void(const QString&, const QJsonObject&)>;

    OpcUaServerRuntime();
    ~OpcUaServerRuntime();

    void setEventCallback(EventCallback callback);

    bool isRunning() const;
    QString endpoint() const;
    QString namespaceUri() const;
    int namespaceIndex() const;
    int nodeCount() const;
    QString eventMode() const;

    bool start(const StartOptions& options, QString& errorMessage);
    bool stop(int gracefulTimeoutMs, QString& errorMessage);
    bool upsertNodes(const QJsonArray& nodes,
                     QJsonArray& results,
                     QString& errorMessage);
    bool deleteNodes(const QStringList& nodeIds,
                     bool recursive,
                     QJsonArray& results,
                     QString& errorMessage);
    bool writeValues(const QJsonArray& items,
                     bool strictType,
                     QJsonArray& results,
                     QString& errorMessage);
    bool inspectNode(const QString& nodeId,
                     bool recurse,
                     QJsonObject& node,
                     QString& errorMessage);
    bool snapshotNodes(const QString& rootNodeId,
                       bool recurse,
                       QJsonObject& node,
                       QString& errorMessage);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

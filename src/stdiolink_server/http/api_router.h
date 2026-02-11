#pragma once

#include <QHttpServer>
#include <QObject>

namespace stdiolink_server {

class ServerManager;

class ApiRouter : public QObject {
    Q_OBJECT
public:
    explicit ApiRouter(ServerManager* manager,
                       QObject* parent = nullptr);

    void registerRoutes(QHttpServer& server);

private:
    QHttpServerResponse handleServiceList(const QHttpServerRequest& req);
    QHttpServerResponse handleServiceDetail(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleServiceScan(const QHttpServerRequest& req);

    QHttpServerResponse handleProjectList(const QHttpServerRequest& req);
    QHttpServerResponse handleProjectDetail(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleProjectCreate(const QHttpServerRequest& req);
    QHttpServerResponse handleProjectUpdate(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleProjectDelete(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleProjectValidate(const QString& id,
                                              const QHttpServerRequest& req);
    QHttpServerResponse handleProjectStart(const QString& id,
                                           const QHttpServerRequest& req);
    QHttpServerResponse handleProjectStop(const QString& id,
                                          const QHttpServerRequest& req);
    QHttpServerResponse handleProjectReload(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleProjectRuntime(const QString& id,
                                             const QHttpServerRequest& req);

    QHttpServerResponse handleInstanceList(const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceTerminate(const QString& id,
                                                const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceLogs(const QString& id,
                                           const QHttpServerRequest& req);

    QHttpServerResponse handleDriverList(const QHttpServerRequest& req);
    QHttpServerResponse handleDriverScan(const QHttpServerRequest& req);

    ServerManager* m_manager = nullptr;
};

} // namespace stdiolink_server

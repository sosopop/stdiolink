#pragma once

#include <QFuture>
#include <QHttpServer>
#include <QHttpServerResponder>
#include <QObject>

namespace stdiolink_server {

class ServerManager;
class StaticFileServer;

class ApiRouter : public QObject {
    Q_OBJECT
public:
    explicit ApiRouter(ServerManager* manager,
                       QObject* parent = nullptr);
    ~ApiRouter() override;

    void registerRoutes(QHttpServer& server);

private:
    QHttpServerResponse handleServiceList(const QHttpServerRequest& req);
    QHttpServerResponse handleServiceCreate(const QHttpServerRequest& req);
    QHttpServerResponse handleServiceDetail(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleServiceDelete(const QString& id,
                                            const QHttpServerRequest& req);
    QHttpServerResponse handleServiceScan(const QHttpServerRequest& req);
    QHttpServerResponse handleServiceFiles(const QString& id,
                                           const QHttpServerRequest& req);
    QHttpServerResponse handleServiceFileRead(const QString& id,
                                              const QHttpServerRequest& req);
    QHttpServerResponse handleServiceFileWrite(const QString& id,
                                               const QHttpServerRequest& req);
    QHttpServerResponse handleServiceFileCreate(const QString& id,
                                                const QHttpServerRequest& req);
    QHttpServerResponse handleServiceFileDelete(const QString& id,
                                                const QHttpServerRequest& req);
    QHttpServerResponse handleValidateSchema(const QString& id,
                                             const QHttpServerRequest& req);
    QHttpServerResponse handleGenerateDefaults(const QString& id,
                                               const QHttpServerRequest& req);
    QHttpServerResponse handleValidateConfig(const QString& id,
                                             const QHttpServerRequest& req);

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
    QHttpServerResponse handleProjectEnabled(const QString& id,
                                             const QHttpServerRequest& req);
    QHttpServerResponse handleProjectLogs(const QString& id,
                                          const QHttpServerRequest& req);
    QHttpServerResponse handleProjectRuntimeBatch(const QHttpServerRequest& req);

    QHttpServerResponse handleInstanceList(const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceTerminate(const QString& id,
                                                const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceLogs(const QString& id,
                                           const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceProcessTree(const QString& id,
                                                   const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceResources(const QString& id,
                                                 const QHttpServerRequest& req);

    QHttpServerResponse handleServerStatus(const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceDetail(const QString& id,
                                             const QHttpServerRequest& req);

    QHttpServerResponse handleDriverList(const QHttpServerRequest& req);
    QHttpServerResponse handleDriverDetail(const QString& id,
                                           const QHttpServerRequest& req);
    QHttpServerResponse handleDriverDocs(const QString& id,
                                         const QHttpServerRequest& req);
    QFuture<QHttpServerResponse> handleDriverScan(const QHttpServerRequest& req);

    void handleEventStream(const QHttpServerRequest& req,
                           QHttpServerResponder& responder);

    ServerManager* m_manager = nullptr;
    StaticFileServer* m_staticFileServer = nullptr;
};

} // namespace stdiolink_server

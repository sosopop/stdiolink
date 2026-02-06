#ifndef DRIVER_SESSION_H
#define DRIVER_SESSION_H

#include <QObject>
#include <QTimer>
#include <memory>
#include <stdiolink/host/driver.h>
#include <stdiolink/host/task.h>
#include <stdiolink/protocol/meta_types.h>

class DriverSession : public QObject
{
    Q_OBJECT

public:
    enum RunMode { OneShot, KeepAlive };
    Q_ENUM(RunMode)

    explicit DriverSession(QObject *parent = nullptr);
    ~DriverSession() override;

    bool start(const QString &program, const QStringList &args = {});
    void stop();
    bool isRunning() const;

    QString program() const { return m_program; }
    const stdiolink::meta::DriverMeta *meta() const;
    bool hasMeta() const;

    RunMode runMode() const { return m_runMode; }
    void setRunMode(RunMode mode);

    void executeCommand(const QString &cmd, const QJsonObject &data = {});
    void cancelCurrentTask();

signals:
    void started();
    void stopped();
    void metaReady(const stdiolink::meta::DriverMeta *meta);
    void messageReceived(const stdiolink::Message &msg);
    void taskCompleted(int exitCode, const QJsonValue &result, const QString &errorText);
    void errorOccurred(const QString &error);

private slots:
    void pollMessages();

private:
    void queryMetaAsync();

    QString m_program;
    std::unique_ptr<stdiolink::Driver> m_driver;
    stdiolink::Task m_currentTask;
    QTimer *m_pollTimer;
    bool m_queryingMeta = false;
    RunMode m_runMode = OneShot;
    std::unique_ptr<stdiolink::meta::DriverMeta> m_cachedMeta;
};

#endif // DRIVER_SESSION_H

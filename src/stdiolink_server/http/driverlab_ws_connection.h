#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QWebSocket>
#include <memory>

namespace stdiolink_server {

class DriverLabWsConnection : public QObject {
    Q_OBJECT
public:
    DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                          const QString& driverId,
                          const QString& program,
                          const QString& runMode,
                          const QStringList& extraArgs,
                          QObject* parent = nullptr);
    ~DriverLabWsConnection() override;

    QString driverId() const { return m_driverId; }

signals:
    void closed(DriverLabWsConnection* conn);

private slots:
    void onTextMessageReceived(const QString& message);
    void onSocketDisconnected();
    void onDriverStdoutReady();
    void onDriverFinished(int exitCode, QProcess::ExitStatus status);
    void onDriverErrorOccurred(QProcess::ProcessError error);

private:
    void startDriver(bool queryMeta = true);
    void stopDriver();
    void sendJson(const QJsonObject& msg);
    void forwardStdoutLine(const QByteArray& line);
    void handleExecMessage(const QJsonObject& msg);
    void handleCancelMessage();
    void restartDriverForOneShot();

    std::unique_ptr<QWebSocket> m_socket;
    std::unique_ptr<QProcess> m_process;
    QString m_driverId;
    QString m_program;
    QString m_runMode;  // "oneshot" | "keepalive"
    QStringList m_extraArgs;
    QByteArray m_stdoutBuffer;
    bool m_metaSent = false;
    bool m_closing = false;

    // OneShot crash backoff
    static constexpr int kMaxRapidCrashes = 3;
    static constexpr int kRapidCrashWindowMs = 2000;
    int m_consecutiveFastCrashes = 0;
    QDateTime m_lastDriverStart;
    bool m_restartSuppressed = false;
    bool m_lastExitWasCrash = false;
};

} // namespace stdiolink_server

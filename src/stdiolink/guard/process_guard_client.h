#pragma once

#include <QString>
#include <QThread>
#include <atomic>
#include <memory>

namespace stdiolink {

class ProcessGuardClient {
public:
    static std::unique_ptr<ProcessGuardClient> startFromArgs(const QStringList& args);

    explicit ProcessGuardClient(const QString& guardName);
    ~ProcessGuardClient();

    ProcessGuardClient(const ProcessGuardClient&) = delete;
    ProcessGuardClient& operator=(const ProcessGuardClient&) = delete;

    void start();
    void stop();

private:
    QString m_guardName;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_stopped{false};
};

} // namespace stdiolink

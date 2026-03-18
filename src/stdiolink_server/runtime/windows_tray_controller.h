#pragma once

#include <QString>

QT_BEGIN_NAMESPACE
class QMenu;
class QSystemTrayIcon;
QT_END_NAMESPACE

namespace stdiolink_server {

class WindowsTrayController {
public:
    explicit WindowsTrayController(const QString& consoleUrl);
    ~WindowsTrayController();

    bool initialize();

private:
    void openConsoleInBrowser();
    void showMenu();

    QString m_consoleUrl;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_menu = nullptr;
};

} // namespace stdiolink_server

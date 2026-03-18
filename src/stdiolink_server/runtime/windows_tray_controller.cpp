#include "runtime/windows_tray_controller.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QUrl>

namespace stdiolink_server {

WindowsTrayController::WindowsTrayController(const QString& consoleUrl)
    : m_consoleUrl(consoleUrl) {}

WindowsTrayController::~WindowsTrayController() {
    if (m_trayIcon != nullptr) {
        m_trayIcon->hide();
        delete m_trayIcon;
    }
    delete m_menu;
}

bool WindowsTrayController::initialize() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;
    }

    m_menu = new QMenu();
    QAction* openConsoleAction = m_menu->addAction(QStringLiteral("打开控制台"));
    QAction* exitAction = m_menu->addAction(QStringLiteral("退出"));

    QObject::connect(openConsoleAction, &QAction::triggered, qApp, [this]() {
        openConsoleInBrowser();
    });
    QObject::connect(exitAction, &QAction::triggered, qApp, []() {
        QCoreApplication::quit();
    });

    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/stdiolink.ico")));
    m_trayIcon->setToolTip(QStringLiteral("stdiolink_server"));
    m_trayIcon->setContextMenu(m_menu);

    QObject::connect(m_trayIcon, &QSystemTrayIcon::activated, qApp, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showMenu();
        }
    });

    m_trayIcon->show();
    return true;
}

void WindowsTrayController::openConsoleInBrowser() {
    const bool opened = QDesktopServices::openUrl(QUrl(m_consoleUrl));
    if (!opened) {
        qWarning("Failed to open browser for %s", qUtf8Printable(m_consoleUrl));
    }
}

void WindowsTrayController::showMenu() {
    if (m_menu != nullptr) {
        m_menu->popup(QCursor::pos());
    }
}

} // namespace stdiolink_server

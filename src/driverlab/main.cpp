#include <QApplication>
#include <QStyleFactory>
#include <QFont>
#include <QPalette>
#include <QFile>
#include "ui/mainwindow.h"

void setStyle(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(248, 249, 250));
    palette.setColor(QPalette::WindowText, QColor(33, 37, 41));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(248, 249, 250));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, QColor(33, 37, 41));
    palette.setColor(QPalette::Button, QColor(248, 249, 250));
    palette.setColor(QPalette::ButtonText, QColor(33, 37, 41));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(0, 123, 255));
    palette.setColor(QPalette::Highlight, QColor(0, 123, 255));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);

    // 设置默认字体
#ifdef Q_OS_WIN
    QFont font("Segoe UI", 9);
#else
    QFont font("Roboto", 9);
#endif
    if (!font.exactMatch()) {
        font = QFont(QApplication::font().family(), 9);
    }
    app.setFont(font);

    // 设置样式表
    app.setStyleSheet(
        "QMainWindow { background-color: #f8f9fa; }"
        "QGroupBox { font-weight: bold; border: 1px solid #dee2e6; border-radius: 4px; margin-top: 12px; padding-top: 10px; background-color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; left: 8px; color: #495057; }"
        "QSplitter::handle { background-color: #e9ecef; width: 2px; height: 2px; }"
        "QTabWidget::pane { border: 1px solid #dee2e6; background-color: white; border-radius: 0 0 4px 4px; }"
        "QTabBar::tab { background: #f1f3f5; border: 1px solid #dee2e6; padding: 6px 12px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; color: #495057; }"
        "QTabBar::tab:selected { background: white; border-bottom-color: white; color: #007bff; font-weight: bold; }"
        "QTextEdit, QPlainTextEdit, QListWidget, QTreeWidget, QTableWidget { border: 1px solid #ced4da; border-radius: 4px; background-color: white; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { border: 1px solid #ced4da; padding: 4px 8px; border-radius: 4px; background: white; min-height: 20px; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #80bdff; }"
        "QPushButton { background-color: #007bff; color: white; border: none; padding: 6px 16px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0069d9; }"
        "QPushButton:pressed { background-color: #0062cc; }"
        "QPushButton:disabled { background-color: #e9ecef; color: #adb5bd; }"
        "QPushButton#secondary { background-color: #6c757d; }"
        "QPushButton#secondary:hover { background-color: #5a6268; }"
        "QToolBar { border-bottom: 1px solid #dee2e6; background: white; spacing: 6px; padding: 4px; }"
        "QStatusBar { background: white; border-top: 1px solid #dee2e6; color: #6c757d; }"
        "QHeaderView::section { background-color: #f8f9fa; border: none; border-bottom: 1px solid #dee2e6; padding: 4px; font-weight: bold; color: #495057; }"
    );
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("DriverLab");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("stdiolink");

    setStyle(app);

    MainWindow window;
    window.show();

    return app.exec();
}
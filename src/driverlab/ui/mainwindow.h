#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QHash>
#include "driver_explorer.h"
#include "driver_test_page.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openDriver();
    void closeCurrentDriver();
    void onTabCloseRequested(int index);
    void onDriverSelected(const QString &id);
    void onDriverDoubleClicked(const QString &id);
    void exportMarkdown();
    void exportHtml();
    void exportOpenAPI();
    void about();

private:
    void setupUi();
    void setupMenus();
    void setupToolBar();
    void updateStatusBar();
    void openDriverByPath(const QString &program);

    DriverTestPage *createTestPage(const QString &program);

    DriverExplorer *m_explorer;
    QTabWidget *m_tabWidget;
    QHash<QString, DriverTestPage*> m_pages;
    int m_driverCounter = 0;
};

#endif // MAINWINDOW_H

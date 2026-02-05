#ifndef DRIVER_TEST_PAGE_H
#define DRIVER_TEST_PAGE_H

#include <QWidget>
#include <QSplitter>
#include "command_list.h"
#include "parameter_form.h"
#include "result_panel.h"
#include "doc_viewer.h"
#include "models/driver_session.h"
#include "models/command_history.h"

class QLabel;

class DriverTestPage : public QWidget
{
    Q_OBJECT

public:
    explicit DriverTestPage(QWidget *parent = nullptr);
    ~DriverTestPage();

    bool openDriver(const QString &program, const QStringList &args = {});
    void closeDriver();

    QString driverName() const;
    bool isRunning() const;
    DriverSession *session() const { return m_session; }

signals:
    void driverStarted();
    void driverStopped();

private slots:
    void onMetaReady(const stdiolink::meta::DriverMeta *meta);
    void onCommandSelected(const stdiolink::meta::CommandMeta *cmd);
    void onExecuteRequested();
    void onMessageReceived(const stdiolink::Message &msg);
    void onTaskCompleted(int exitCode, const QJsonValue &result, const QString &errorText);

private:
    void setupUi();

    DriverSession *m_session;
    CommandHistory *m_history;

    QLabel *m_headerLabel;
    QLabel *m_statusLabel;
    CommandList *m_commandList;
    ParameterForm *m_paramForm;
    DocViewer *m_docViewer;
    ResultPanel *m_resultPanel;

    QString m_currentCommand;
    QDateTime m_commandStartTime;
};

#endif // DRIVER_TEST_PAGE_H

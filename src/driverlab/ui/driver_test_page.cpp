#include "driver_test_page.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QGroupBox>
#include <QApplication>
#include <QStyle>

DriverTestPage::DriverTestPage(QWidget *parent)
    : QWidget(parent)
    , m_session(new DriverSession(this))
    , m_history(new CommandHistory(this))
{
    setupUi();

    connect(m_session, &DriverSession::metaReady,
            this, &DriverTestPage::onMetaReady);
    connect(m_session, &DriverSession::messageReceived,
            this, &DriverTestPage::onMessageReceived);
    connect(m_session, &DriverSession::taskCompleted,
            this, &DriverTestPage::onTaskCompleted);
    connect(m_session, &DriverSession::started,
            this, &DriverTestPage::driverStarted);
    connect(m_session, &DriverSession::stopped,
            this, &DriverTestPage::driverStopped);

    connect(m_commandList, &CommandList::commandSelected,
            this, &DriverTestPage::onCommandSelected);
    connect(m_paramForm, &ParameterForm::executeRequested,
            this, &DriverTestPage::onExecuteRequested);
}

DriverTestPage::~DriverTestPage()
{
    // 先断开所有信号连接，防止析构时信号触发
    disconnect(m_session, nullptr, this, nullptr);
    closeDriver();
}

void DriverTestPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Header
    auto *headerLayout = new QHBoxLayout;
    m_headerLabel = new QLabel(tr("未加载 Driver"), this);
    m_headerLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
    
    m_statusLabel = new QLabel(this);
    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(headerLayout);

    // Main splitter
    auto *mainSplitter = new QSplitter(Qt::Vertical, this);
    mainSplitter->setHandleWidth(2);

    // Top area
    auto *topSplitter = new QSplitter(Qt::Horizontal);
    topSplitter->setHandleWidth(2);

    // Command list
    auto *cmdGroup = new QGroupBox(tr("命令列表"), this);
    auto *cmdLayout = new QVBoxLayout(cmdGroup);
    cmdLayout->setContentsMargins(5, 10, 5, 5);
    m_commandList = new CommandList(this);
    cmdLayout->addWidget(m_commandList);
    topSplitter->addWidget(cmdGroup);

    // Right panel (form + doc)
    auto *rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->setHandleWidth(2);

    auto *formGroup = new QGroupBox(tr("参数设置"), this);
    auto *formLayout = new QVBoxLayout(formGroup);
    formLayout->setContentsMargins(0, 10, 0, 0); // 让 ScrollArea 贴边
    m_paramForm = new ParameterForm(this);
    formLayout->addWidget(m_paramForm);
    rightSplitter->addWidget(formGroup);

    auto *docGroup = new QGroupBox(tr("命令文档"), this);
    auto *docLayout = new QVBoxLayout(docGroup);
    docLayout->setContentsMargins(5, 10, 5, 5);
    m_docViewer = new DocViewer(this);
    docLayout->addWidget(m_docViewer);
    rightSplitter->addWidget(docGroup);

    topSplitter->addWidget(rightSplitter);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 3);

    mainSplitter->addWidget(topSplitter);

    // Result panel
    auto *resultGroup = new QGroupBox(tr("执行结果"), this);
    auto *resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->setContentsMargins(5, 10, 5, 5);
    m_resultPanel = new ResultPanel(this);
    resultLayout->addWidget(m_resultPanel);
    mainSplitter->addWidget(resultGroup);

    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 2);

    mainLayout->addWidget(mainSplitter);
}

bool DriverTestPage::openDriver(const QString &program, const QStringList &args)
{
    closeDriver();
    if (m_session->start(program, args)) {
        m_headerLabel->setText(program);
        m_statusLabel->setText(tr("运行中"));
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        return true;
    }
    return false;
}

void DriverTestPage::closeDriver()
{
    m_session->stop();
    m_statusLabel->setText(tr("已停止"));
    m_statusLabel->setStyleSheet("color: gray; font-weight: bold;");
}

QString DriverTestPage::driverName() const
{
    return m_headerLabel->text();
}

bool DriverTestPage::isRunning() const
{
    return m_session->isRunning();
}

void DriverTestPage::onMetaReady(const stdiolink::meta::DriverMeta *meta)
{
    if (!meta) return;

    QString title = meta->info.name;
    if (!meta->info.version.isEmpty()) {
        title += " v" + meta->info.version;
    }
    m_headerLabel->setText(title);
    m_commandList->setCommands(meta->commands);
}

void DriverTestPage::onCommandSelected(const stdiolink::meta::CommandMeta *cmd)
{
    m_paramForm->setCommand(cmd);
    m_docViewer->setCommand(cmd);
    m_resultPanel->clear();
    if (cmd) {
        m_currentCommand = cmd->name;
    }
}

void DriverTestPage::onExecuteRequested()
{
    if (m_currentCommand.isEmpty()) return;
    if (!m_paramForm->validate()) return;

    m_resultPanel->clear();
    m_commandStartTime = QDateTime::currentDateTime();

    QJsonObject data = m_paramForm->collectData();
    m_session->executeCommand(m_currentCommand, data);
}

void DriverTestPage::onMessageReceived(const stdiolink::Message &msg)
{
    m_resultPanel->addMessage(msg);
}

void DriverTestPage::onTaskCompleted(int exitCode, const QJsonValue &result, const QString &errorText)
{
    qint64 duration = m_commandStartTime.msecsTo(QDateTime::currentDateTime());

    HistoryEntry entry;
    entry.command = m_currentCommand;
    entry.params = m_paramForm->collectData();
    entry.timestamp = m_commandStartTime;
    entry.exitCode = exitCode;
    entry.result = result;
    entry.errorText = errorText;
    entry.durationMs = duration;

    m_history->addEntry(entry);
    m_resultPanel->addHistoryEntry(entry);
}
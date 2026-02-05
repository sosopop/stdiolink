#include "mainwindow.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QJsonDocument>
#include <QStyle>
#include "stdiolink/doc/doc_generator.h"
#include "widgets/emoji_icon.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupMenus();
    setupToolBar();

    setWindowTitle(tr("DriverLab - Driver 测试工具"));
    resize(1280, 800);

    statusBar()->showMessage(tr("就绪"));
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    // Explorer dock
    auto *dock = new QDockWidget(tr("Driver 浏览器"), this);
    m_explorer = new DriverExplorer(dock);
    dock->setWidget(m_explorer);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true); // 让标签页风格更现代
    setCentralWidget(m_tabWidget);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

    // Explorer 信号连接
    connect(m_explorer, &DriverExplorer::driverSelected,
            this, &MainWindow::onDriverSelected);
    connect(m_explorer, &DriverExplorer::driverDoubleClicked,
            this, &MainWindow::onDriverDoubleClicked);
}

void MainWindow::setupMenus()
{
    // File menu
    auto *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(EmojiIcon::get("📂"), tr("打开 Driver(&O)..."), QKeySequence::Open, this, &MainWindow::openDriver);
    fileMenu->addAction(EmojiIcon::get("❌"), tr("关闭 Driver(&C)"), QKeySequence::Close, this, &MainWindow::closeCurrentDriver);
    fileMenu->addSeparator();
    fileMenu->addAction(EmojiIcon::get("🚪"), tr("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    // Export menu
    auto *exportMenu = menuBar()->addMenu(tr("导出(&E)"));
    exportMenu->addAction(EmojiIcon::get("📝"), tr("导出 Markdown(&M)..."), this, &MainWindow::exportMarkdown);
    exportMenu->addAction(EmojiIcon::get("🌐"), tr("导出 HTML(&H)..."), this, &MainWindow::exportHtml);
    exportMenu->addAction(EmojiIcon::get("🔌"), tr("导出 OpenAPI(&O)..."), this, &MainWindow::exportOpenAPI);

    // Help menu
    auto *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(EmojiIcon::get("💡"), tr("关于(&A)"), this, &MainWindow::about);
}

void MainWindow::setupToolBar()
{
    auto *toolBar = addToolBar(tr("主工具栏"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    
    toolBar->addAction(EmojiIcon::get("📂"), tr("打开"), this, &MainWindow::openDriver);
    toolBar->addAction(EmojiIcon::get("❌"), tr("关闭"), this, &MainWindow::closeCurrentDriver);
}

void MainWindow::openDriver()
{
    QString program = QFileDialog::getOpenFileName(
        this, tr("打开 Driver"),
        QString(),
        tr("可执行文件 (*.exe);;所有文件 (*)")
    );

    if (program.isEmpty()) return;
    openDriverByPath(program);
}

void MainWindow::openDriverByPath(const QString &program)
{
    auto *page = createTestPage(program);
    if (page) {
        QString id = QString("driver_%1").arg(++m_driverCounter);
        m_pages[id] = page;

        QFileInfo fi(program);
        int index = m_tabWidget->addTab(page, fi.baseName());
        m_tabWidget->setCurrentIndex(index);

        m_explorer->addDriver(id, fi.baseName(), true);
        updateStatusBar();
    }
}

void MainWindow::closeCurrentDriver()
{
    int index = m_tabWidget->currentIndex();
    if (index >= 0) {
        onTabCloseRequested(index);
    }
}

void MainWindow::onTabCloseRequested(int index)
{
    auto *page = qobject_cast<DriverTestPage*>(m_tabWidget->widget(index));
    if (page) {
        page->closeDriver();
        m_tabWidget->removeTab(index);

        QString id = m_pages.key(page);
        if (!id.isEmpty()) {
            m_explorer->removeDriver(id);
            m_pages.remove(id);
        }
        page->deleteLater();
        updateStatusBar();
    }
}

void MainWindow::onDriverSelected(const QString &id)
{
    // 切换到对应的标签页
    if (m_pages.contains(id)) {
        auto *page = m_pages[id];
        int index = m_tabWidget->indexOf(page);
        if (index >= 0) {
            m_tabWidget->setCurrentIndex(index);
        }
    }
}

void MainWindow::onDriverDoubleClicked(const QString &id)
{
    // 如果是已加载的 Driver，切换到标签页
    if (m_pages.contains(id)) {
        onDriverSelected(id);
        return;
    }

    // 如果是 Registry 中的 Driver，打开它
    QString program = m_explorer->getRegistryDriverPath(id);
    if (!program.isEmpty()) {
        openDriverByPath(program);
    }
}

void MainWindow::exportMarkdown()
{
    auto *page = qobject_cast<DriverTestPage*>(m_tabWidget->currentWidget());
    if (!page || !page->isRunning()) {
        QMessageBox::warning(this, tr("导出"), tr("没有活动的 Driver"));
        return;
    }

    auto *session = page->session();
    if (!session || !session->hasMeta()) {
        QMessageBox::warning(this, tr("导出"), tr("Driver 元数据不可用"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出 Markdown"),
        page->driverName() + ".md",
        tr("Markdown 文件 (*.md)")
    );
    if (path.isEmpty()) return;

    const auto *meta = session->meta();
    QString content = stdiolink::DocGenerator::toMarkdown(*meta);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        statusBar()->showMessage(tr("已导出到 %1").arg(path), 3000);
    } else {
        QMessageBox::critical(this, tr("导出"), tr("写入文件失败"));
    }
}

void MainWindow::exportHtml()
{
    auto *page = qobject_cast<DriverTestPage*>(m_tabWidget->currentWidget());
    if (!page || !page->isRunning()) {
        QMessageBox::warning(this, tr("导出"), tr("没有活动的 Driver"));
        return;
    }

    auto *session = page->session();
    if (!session || !session->hasMeta()) {
        QMessageBox::warning(this, tr("导出"), tr("Driver 元数据不可用"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出 HTML"),
        page->driverName() + ".html",
        tr("HTML 文件 (*.html)")
    );
    if (path.isEmpty()) return;

    const auto *meta = session->meta();
    QString content = stdiolink::DocGenerator::toHtml(*meta);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        statusBar()->showMessage(tr("已导出到 %1").arg(path), 3000);
    } else {
        QMessageBox::critical(this, tr("导出"), tr("写入文件失败"));
    }
}

void MainWindow::exportOpenAPI()
{
    auto *page = qobject_cast<DriverTestPage*>(m_tabWidget->currentWidget());
    if (!page || !page->isRunning()) {
        QMessageBox::warning(this, tr("导出"), tr("没有活动的 Driver"));
        return;
    }

    auto *session = page->session();
    if (!session || !session->hasMeta()) {
        QMessageBox::warning(this, tr("导出"), tr("Driver 元数据不可用"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出 OpenAPI"),
        page->driverName() + ".json",
        tr("JSON 文件 (*.json)")
    );
    if (path.isEmpty()) return;

    const auto *meta = session->meta();
    QJsonObject apiObj = stdiolink::DocGenerator::toOpenAPI(*meta);
    QJsonDocument doc(apiObj);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        statusBar()->showMessage(tr("已导出到 %1").arg(path), 3000);
    } else {
        QMessageBox::critical(this, tr("导出"), tr("写入文件失败"));
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("关于 DriverLab"),
        tr("DriverLab v1.0.0\n\n"
           "stdiolink Driver 测试工具\n\n"
           "stdiolink 项目的一部分"));
}

void MainWindow::updateStatusBar()
{
    int running = 0;
    for (auto *page : m_pages) {
        if (page->isRunning()) ++running;
    }
    statusBar()->showMessage(tr("%1 个 Driver 运行中").arg(running));
}

DriverTestPage *MainWindow::createTestPage(const QString &program)
{
    auto *page = new DriverTestPage(this);
    if (!page->openDriver(program)) {
        delete page;
        return nullptr;
    }
    return page;
}

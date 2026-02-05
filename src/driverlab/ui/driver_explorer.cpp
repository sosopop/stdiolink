#include "driver_explorer.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStyle>
#include <QApplication>

DriverExplorer::DriverExplorer(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    m_tree->setHeaderLabel(tr("Driver 列表"));
    m_tree->header()->setVisible(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(20); // 增加缩进，更清晰

    m_loadedItem = new QTreeWidgetItem(m_tree, {tr("运行中")});
    m_loadedItem->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    
    m_registryItem = new QTreeWidgetItem(m_tree, {tr("收藏夹")});
    m_registryItem->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    
    m_loadedItem->setExpanded(true);
    m_registryItem->setExpanded(true);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (item->parent()) {
            emit driverSelected(item->data(0, Qt::UserRole).toString());
        }
    });

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item->parent()) {
            emit driverDoubleClicked(item->data(0, Qt::UserRole).toString());
        }
    });

    setupContextMenu();
    loadRegistry();
}

void DriverExplorer::addDriver(const QString &id, const QString &name, bool running)
{
    auto *item = new QTreeWidgetItem(m_loadedItem);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, id);
    setDriverStatus(id, running);
}

void DriverExplorer::removeDriver(const QString &id)
{
    for (int i = 0; i < m_loadedItem->childCount(); ++i) {
        auto *item = m_loadedItem->child(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            delete m_loadedItem->takeChild(i);
            break;
        }
    }
}

void DriverExplorer::setDriverStatus(const QString &id, bool running)
{
    for (int i = 0; i < m_loadedItem->childCount(); ++i) {
        auto *item = m_loadedItem->child(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            // 使用标准图标模拟状态
            item->setIcon(0, QApplication::style()->standardIcon(
                running ? QStyle::SP_MediaPlay : QStyle::SP_MediaStop
            ));
            break;
        }
    }
}

void DriverExplorer::clear()
{
    while (m_loadedItem->childCount() > 0) {
        delete m_loadedItem->takeChild(0);
    }
}

void DriverExplorer::setupContextMenu()
{
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        QMenu menu;

        if (item == m_registryItem) {
            menu.addAction(tr("添加 Driver..."), this, [this]() {
                QStringList paths = QFileDialog::getOpenFileNames(
                    this, tr("选择 Driver"),
                    QString(),
                    tr("可执行文件 (*.exe);;所有文件 (*)")
                );
                for (const QString &path : paths) {
                    QFileInfo fi(path);
                    addToRegistry(fi.baseName(), path);
                }
            });
        } else if (item && item->parent() == m_registryItem) {
            QString id = item->data(0, Qt::UserRole).toString();
            menu.addAction(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"), this, [this, id]() {
                emit driverDoubleClicked(id);
            });
            menu.addAction(QApplication::style()->standardIcon(QStyle::SP_TrashIcon), tr("移除"), this, [this, id]() {
                removeFromRegistry(id);
            });
        }

        if (!menu.isEmpty()) {
            menu.exec(m_tree->viewport()->mapToGlobal(pos));
        }
    });
}

void DriverExplorer::loadRegistry()
{
    QSettings settings("stdiolink", "DriverLab");
    int count = settings.beginReadArray("registry");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RegistryEntry entry;
        entry.id = QString("reg_%1").arg(i);
        entry.name = settings.value("name").toString();
        entry.path = settings.value("path").toString();
        m_registryEntries.append(entry);

        auto *item = new QTreeWidgetItem(m_registryItem);
        item->setText(0, entry.name);
        item->setData(0, Qt::UserRole, entry.id);
        item->setToolTip(0, entry.path);
        item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    }
    settings.endArray();
}

void DriverExplorer::saveRegistry()
{
    QSettings settings("stdiolink", "DriverLab");
    settings.beginWriteArray("registry");
    for (int i = 0; i < m_registryEntries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", m_registryEntries[i].name);
        settings.setValue("path", m_registryEntries[i].path);
    }
    settings.endArray();
}

void DriverExplorer::addToRegistry(const QString &name, const QString &path)
{
    RegistryEntry entry;
    entry.id = QString("reg_%1").arg(m_registryEntries.size());
    entry.name = name;
    entry.path = path;
    m_registryEntries.append(entry);

    auto *item = new QTreeWidgetItem(m_registryItem);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, entry.id);
    item->setToolTip(0, path);
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));

    saveRegistry();
}

void DriverExplorer::removeFromRegistry(const QString &id)
{
    for (int i = 0; i < m_registryEntries.size(); ++i) {
        if (m_registryEntries[i].id == id) {
            m_registryEntries.removeAt(i);
            break;
        }
    }

    for (int i = 0; i < m_registryItem->childCount(); ++i) {
        auto *item = m_registryItem->child(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            delete m_registryItem->takeChild(i);
            break;
        }
    }

    saveRegistry();
}

QString DriverExplorer::getRegistryDriverPath(const QString &id) const
{
    for (const auto &entry : m_registryEntries) {
        if (entry.id == id) {
            return entry.path;
        }
    }
    return QString();
}
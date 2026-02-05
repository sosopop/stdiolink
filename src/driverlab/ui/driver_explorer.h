#ifndef DRIVER_EXPLORER_H
#define DRIVER_EXPLORER_H

#include <QWidget>
#include <QTreeWidget>
#include <QSettings>

struct RegistryEntry {
    QString id;
    QString name;
    QString path;
};

class DriverExplorer : public QWidget
{
    Q_OBJECT

public:
    explicit DriverExplorer(QWidget *parent = nullptr);

    // Loaded drivers
    void addDriver(const QString &id, const QString &name, bool running);
    void removeDriver(const QString &id);
    void setDriverStatus(const QString &id, bool running);
    void clear();

    // Registry
    void loadRegistry();
    void saveRegistry();
    void addToRegistry(const QString &name, const QString &path);
    void removeFromRegistry(const QString &id);
    QString getRegistryDriverPath(const QString &id) const;

signals:
    void driverSelected(const QString &id);
    void driverDoubleClicked(const QString &id);

private:
    void setupContextMenu();

    QTreeWidget *m_tree;
    QTreeWidgetItem *m_loadedItem;
    QTreeWidgetItem *m_registryItem;
    QList<RegistryEntry> m_registryEntries;
};

#endif // DRIVER_EXPLORER_H

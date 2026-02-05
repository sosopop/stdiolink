#ifndef JSON_TREE_WIDGET_H
#define JSON_TREE_WIDGET_H

#include <QTreeWidget>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>

class JsonTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit JsonTreeWidget(QWidget *parent = nullptr);

    void setJson(const QJsonValue &json);
    void clear();

private slots:
    void onItemExpanded(QTreeWidgetItem *item);

private:
    void addNodeLazy(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value);
    void populateChildren(QTreeWidgetItem *item);
    QString valueToString(const QJsonValue &value);
    QString getTypeString(const QJsonValue &value);

    static constexpr int kJsonDataRole = Qt::UserRole + 1;
    static constexpr int kLoadedRole = Qt::UserRole + 2;
};

#endif // JSON_TREE_WIDGET_H

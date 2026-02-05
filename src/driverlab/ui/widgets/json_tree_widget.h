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

private:
    void addNode(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value);
    QString valueToString(const QJsonValue &value);
    QIcon getIconForValue(const QJsonValue &value);
};

#endif // JSON_TREE_WIDGET_H

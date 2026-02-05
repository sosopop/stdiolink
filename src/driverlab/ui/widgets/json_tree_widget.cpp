#include "json_tree_widget.h"
#include <QHeaderView>
#include <QApplication>
#include <QStyle>

JsonTreeWidget::JsonTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderLabels({tr("键"), tr("值"), tr("类型")});
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    setAlternatingRowColors(true);
    setAnimated(true);
}

void JsonTreeWidget::setJson(const QJsonValue &json)
{
    clear();
    addNode(invisibleRootItem(), "root", json);
    expandToDepth(1);
}

void JsonTreeWidget::clear()
{
    QTreeWidget::clear();
}

void JsonTreeWidget::addNode(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, key);
    item->setIcon(0, getIconForValue(value));

    if (value.isObject()) {
        item->setText(2, "Object");
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            addNode(item, it.key(), it.value());
        }
    } else if (value.isArray()) {
        item->setText(2, "Array");
        QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            addNode(item, QString("[%1]").arg(i), arr[i]);
        }
    } else {
        item->setText(1, valueToString(value));
        if (value.isString()) item->setText(2, "String");
        else if (value.isDouble()) item->setText(2, "Number");
        else if (value.isBool()) item->setText(2, "Bool");
        else if (value.isNull()) item->setText(2, "Null");
    }
}

QString JsonTreeWidget::valueToString(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble());
    if (value.isBool()) return value.toBool() ? "true" : "false";
    if (value.isNull()) return "null";
    return QString();
}

QIcon JsonTreeWidget::getIconForValue(const QJsonValue &value)
{
    if (value.isObject()) return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    if (value.isArray()) return QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView);
    return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
}

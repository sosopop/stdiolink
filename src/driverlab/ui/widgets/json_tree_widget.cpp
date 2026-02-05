#include "json_tree_widget.h"
#include <QHeaderView>
#include <QJsonDocument>

JsonTreeWidget::JsonTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderLabels({tr("键"), tr("值"), tr("类型")});
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    setAlternatingRowColors(true);
    setAnimated(false);
    setUniformRowHeights(true);

    connect(this, &QTreeWidget::itemExpanded, this, &JsonTreeWidget::onItemExpanded);
}

void JsonTreeWidget::setJson(const QJsonValue &json)
{
    setUpdatesEnabled(false);
    clear();
    addNodeLazy(invisibleRootItem(), "root", json);
    setUpdatesEnabled(true);

    // 展开根节点
    if (topLevelItemCount() > 0) {
        topLevelItem(0)->setExpanded(true);
    }
}

void JsonTreeWidget::clear()
{
    QTreeWidget::clear();
}

void JsonTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    // 检查是否已加载子节点
    if (item->data(0, kLoadedRole).toBool()) {
        return;
    }

    populateChildren(item);
}

void JsonTreeWidget::addNodeLazy(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, key);
    item->setText(2, getTypeString(value));

    // 存储 JSON 数据用于延迟加载
    QJsonDocument doc;
    if (value.isObject()) {
        doc.setObject(value.toObject());
    } else if (value.isArray()) {
        doc.setArray(value.toArray());
    }
    item->setData(0, kJsonDataRole, doc.toJson(QJsonDocument::Compact));
    item->setData(0, kLoadedRole, false);

    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        item->setText(1, QString("{%1 项}").arg(obj.size()));
        // 添加占位子节点，使其可展开
        if (!obj.isEmpty()) {
            new QTreeWidgetItem(item);
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        item->setText(1, QString("[%1 项]").arg(arr.size()));
        if (!arr.isEmpty()) {
            new QTreeWidgetItem(item);
        }
    } else {
        item->setText(1, valueToString(value));
        item->setData(0, kLoadedRole, true); // 叶子节点标记为已加载
    }
}

void JsonTreeWidget::populateChildren(QTreeWidgetItem *item)
{
    // 移除占位子节点
    while (item->childCount() > 0) {
        delete item->takeChild(0);
    }

    // 解析存储的 JSON 数据
    QString jsonStr = item->data(0, kJsonDataRole).toString();
    if (jsonStr.isEmpty()) {
        item->setData(0, kLoadedRole, true);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());

    setUpdatesEnabled(false);

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            addNodeLazy(item, it.key(), it.value());
        }
    } else if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (int i = 0; i < arr.size(); ++i) {
            addNodeLazy(item, QString("[%1]").arg(i), arr[i]);
        }
    }

    setUpdatesEnabled(true);
    item->setData(0, kLoadedRole, true);
}

QString JsonTreeWidget::valueToString(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isDouble()) {
        double d = value.toDouble();
        if (d == static_cast<qint64>(d)) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d);
    }
    if (value.isBool()) return value.toBool() ? "true" : "false";
    if (value.isNull()) return "null";
    return QString();
}

QString JsonTreeWidget::getTypeString(const QJsonValue &value)
{
    if (value.isObject()) return "Object";
    if (value.isArray()) return "Array";
    if (value.isString()) return "String";
    if (value.isDouble()) return "Number";
    if (value.isBool()) return "Bool";
    if (value.isNull()) return "Null";
    return "Unknown";
}

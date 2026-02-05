#ifndef ARRAY_EDITOR_H
#define ARRAY_EDITOR_H

#include <QWidget>
#include <QJsonArray>
#include <QListWidget>
#include <stdiolink/protocol/meta_types.h>

class ArrayEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ArrayEditor(const stdiolink::meta::FieldMeta &field, QWidget *parent = nullptr);

    QJsonValue value() const;
    void setValue(const QJsonArray &arr);

signals:
    void valueChanged();

private slots:
    void addItem();
    void removeItem();

private:
    stdiolink::meta::FieldMeta m_field;
    QListWidget *m_list;
};

#endif // ARRAY_EDITOR_H

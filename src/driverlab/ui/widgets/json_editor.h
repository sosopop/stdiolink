#ifndef JSON_EDITOR_H
#define JSON_EDITOR_H

#include <QWidget>
#include <QJsonValue>
#include <QPlainTextEdit>

class JsonEditor : public QWidget
{
    Q_OBJECT

public:
    explicit JsonEditor(QWidget *parent = nullptr);

    QJsonValue value() const;
    void setValue(const QJsonValue &val);
    bool isValid() const;

signals:
    void valueChanged();

private:
    QPlainTextEdit *m_edit;
};

#endif // JSON_EDITOR_H

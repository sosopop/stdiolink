#include "json_editor.h"

#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

JsonEditor::JsonEditor(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QPlainTextEdit(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_edit);

    m_edit->setMaximumHeight(120);
    m_edit->setPlaceholderText("JSON...");

    connect(m_edit, &QPlainTextEdit::textChanged, this, &JsonEditor::valueChanged);
}

QJsonValue JsonEditor::value() const
{
    QString text = m_edit->toPlainText().trimmed();
    if (text.isEmpty()) return QJsonValue();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return QJsonValue(text);
    }

    if (doc.isArray()) return doc.array();
    if (doc.isObject()) return doc.object();
    return QJsonValue();
}

void JsonEditor::setValue(const QJsonValue &val)
{
    QJsonDocument doc;
    if (val.isArray()) doc = QJsonDocument(val.toArray());
    else if (val.isObject()) doc = QJsonDocument(val.toObject());
    else {
        m_edit->setPlainText(val.toString());
        return;
    }
    m_edit->setPlainText(doc.toJson(QJsonDocument::Indented));
}

bool JsonEditor::isValid() const
{
    QString text = m_edit->toPlainText().trimmed();
    if (text.isEmpty()) return true;

    QJsonParseError err;
    QJsonDocument::fromJson(text.toUtf8(), &err);
    return err.error == QJsonParseError::NoError;
}

#include "form_widget_factory.h"
#include "json_editor.h"
#include "array_editor.h"

#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>

FormWidgetFactory::WidgetInfo FormWidgetFactory::createWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    using namespace stdiolink::meta;

    switch (field.type) {
    case FieldType::String:
        return createStringWidget(field, parent);
    case FieldType::Int:
    case FieldType::Int64:
        return createIntWidget(field, parent);
    case FieldType::Double:
        return createDoubleWidget(field, parent);
    case FieldType::Bool:
        return createBoolWidget(field, parent);
    case FieldType::Enum:
        return createEnumWidget(field, parent);
    case FieldType::Array:
        return createArrayWidget(field, parent);
    case FieldType::Object:
        return createObjectWidget(field, parent);
    case FieldType::Any:
    default:
        return createAnyWidget(field, parent);
    }
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createStringWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    bool multiline = field.ui.widget == "textarea" ||
                     (field.constraints.maxLength && *field.constraints.maxLength > 200);

    if (multiline) {
        auto *edit = new QTextEdit(parent);
        edit->setPlaceholderText(field.ui.placeholder);
        edit->setMaximumHeight(100);
        info.widget = edit;
        info.getValue = [edit]() { return QJsonValue(edit->toPlainText()); };
        info.setValue = [edit](const QJsonValue &v) { edit->setPlainText(v.toString()); };
    } else {
        auto *edit = new QLineEdit(parent);
        edit->setPlaceholderText(field.ui.placeholder);
        if (!field.defaultValue.isNull()) {
            edit->setText(field.defaultValue.toString());
        }
        info.widget = edit;
        info.getValue = [edit]() { return QJsonValue(edit->text()); };
        info.setValue = [edit](const QJsonValue &v) { edit->setText(v.toString()); };
    }

    info.validate = [&field, info]() {
        QString val = info.getValue().toString();
        if (field.required && val.isEmpty()) return false;
        return true;
    };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createIntWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *spin = new QSpinBox(parent);
    if (field.constraints.min) spin->setMinimum(static_cast<int>(*field.constraints.min));
    if (field.constraints.max) spin->setMaximum(static_cast<int>(*field.constraints.max));
    if (!field.defaultValue.isNull()) spin->setValue(field.defaultValue.toInt());

    info.widget = spin;
    info.getValue = [spin]() { return QJsonValue(spin->value()); };
    info.setValue = [spin](const QJsonValue &v) { spin->setValue(v.toInt()); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createDoubleWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(6);
    if (field.constraints.min) spin->setMinimum(*field.constraints.min);
    if (field.constraints.max) spin->setMaximum(*field.constraints.max);
    if (field.ui.step > 0) spin->setSingleStep(field.ui.step);
    if (!field.defaultValue.isNull()) spin->setValue(field.defaultValue.toDouble());

    info.widget = spin;
    info.getValue = [spin]() { return QJsonValue(spin->value()); };
    info.setValue = [spin](const QJsonValue &v) { spin->setValue(v.toDouble()); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createBoolWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    info.label = new QLabel("", parent);

    auto *check = new QCheckBox(field.name, parent);
    if (!field.defaultValue.isNull()) check->setChecked(field.defaultValue.toBool());

    info.widget = check;
    info.getValue = [check]() { return QJsonValue(check->isChecked()); };
    info.setValue = [check](const QJsonValue &v) { check->setChecked(v.toBool()); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createEnumWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *combo = new QComboBox(parent);
    for (const auto &val : field.constraints.enumValues) {
        combo->addItem(val.toString());
    }

    info.widget = combo;
    info.getValue = [combo]() { return QJsonValue(combo->currentText()); };
    info.setValue = [combo](const QJsonValue &v) { combo->setCurrentText(v.toString()); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createArrayWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *editor = new ArrayEditor(field, parent);
    info.widget = editor;
    info.getValue = [editor]() { return editor->value(); };
    info.setValue = [editor](const QJsonValue &v) { editor->setValue(v.toArray()); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createObjectWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *editor = new JsonEditor(parent);
    info.widget = editor;
    info.getValue = [editor]() { return editor->value(); };
    info.setValue = [editor](const QJsonValue &v) { editor->setValue(v); };
    info.validate = []() { return true; };

    return info;
}

FormWidgetFactory::WidgetInfo FormWidgetFactory::createAnyWidget(
    const stdiolink::meta::FieldMeta &field, QWidget *parent)
{
    WidgetInfo info;
    QString labelText = field.name;
    if (field.required) labelText += " *";
    info.label = new QLabel(labelText, parent);

    auto *editor = new JsonEditor(parent);
    info.widget = editor;
    info.getValue = [editor]() { return editor->value(); };
    info.setValue = [editor](const QJsonValue &v) { editor->setValue(v); };
    info.validate = []() { return true; };

    return info;
}
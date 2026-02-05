#ifndef FORM_WIDGET_FACTORY_H
#define FORM_WIDGET_FACTORY_H

#include <QWidget>
#include <QJsonValue>
#include <stdiolink/protocol/meta_types.h>

class FormWidgetFactory
{
public:
    struct WidgetInfo {
        QWidget *widget = nullptr;
        QWidget *label = nullptr;
        std::function<QJsonValue()> getValue;
        std::function<void(const QJsonValue &)> setValue;
        std::function<bool()> validate;
    };

    static WidgetInfo createWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);

private:
    static WidgetInfo createStringWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createIntWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createDoubleWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createBoolWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createEnumWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createArrayWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createObjectWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
    static WidgetInfo createAnyWidget(const stdiolink::meta::FieldMeta &field, QWidget *parent);
};

#endif // FORM_WIDGET_FACTORY_H

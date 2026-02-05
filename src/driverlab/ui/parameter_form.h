#ifndef PARAMETER_FORM_H
#define PARAMETER_FORM_H

#include <QWidget>
#include <QJsonObject>
#include <QVector>
#include <QScrollArea>
#include <stdiolink/protocol/meta_types.h>
#include "widgets/form_widget_factory.h"

class QFormLayout;

class ParameterForm : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterForm(QWidget *parent = nullptr);

    void setCommand(const stdiolink::meta::CommandMeta *cmd);
    void clear();

    QJsonObject collectData() const;
    bool validate() const;

signals:
    void executeRequested();

private:
    void buildForm();

    const stdiolink::meta::CommandMeta *m_command = nullptr;
    QFormLayout *m_formLayout;
    QVector<FormWidgetFactory::WidgetInfo> m_widgets;
    QVector<stdiolink::meta::FieldMeta> m_fields;
};

#endif // PARAMETER_FORM_H
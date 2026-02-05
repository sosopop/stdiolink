#ifndef PARAMETER_FORM_H
#define PARAMETER_FORM_H

#include <QWidget>
#include <QJsonObject>
#include <QVector>
#include <QScrollArea>
#include <stdiolink/protocol/meta_types.h>
#include "widgets/form_widget_factory.h"

class QFormLayout;
class QLineEdit;

class ParameterForm : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterForm(QWidget *parent = nullptr);

    void setDriverProgram(const QString &program);
    void setCommand(const stdiolink::meta::CommandMeta *cmd);
    void clear();

    QJsonObject collectData() const;
    bool validate() const;

signals:
    void executeRequested();

private slots:
    void updateCommandLineExample();

private:
    void buildForm();
    QString escapeShellArg(const QString &arg) const;

    QString m_driverProgram;
    const stdiolink::meta::CommandMeta *m_command = nullptr;
    QFormLayout *m_formLayout;
    QVector<FormWidgetFactory::WidgetInfo> m_widgets;
    QVector<stdiolink::meta::FieldMeta> m_fields;
    QLineEdit *m_cmdLineEdit;
};

#endif // PARAMETER_FORM_H
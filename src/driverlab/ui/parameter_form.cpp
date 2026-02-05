#include "parameter_form.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QApplication>
#include <QStyle>
#include "widgets/emoji_icon.h"

ParameterForm::ParameterForm(QWidget *parent)
    : QWidget(parent)
    , m_formLayout(new QFormLayout)
{
    auto *layout = new QVBoxLayout(this);
    
    // 使用 ScrollArea 防止内容过多时无法显示
    QWidget *scrollContent = new QWidget;
    m_formLayout->setContentsMargins(10, 10, 10, 10);
    m_formLayout->setSpacing(10);
    scrollContent->setLayout(m_formLayout);
    
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(scrollContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scrollArea);

    auto *btnLayout = new QHBoxLayout;
    auto *execBtn = new QPushButton(tr("执行"), this);
    execBtn->setIcon(EmojiIcon::get("⚡"));
    execBtn->setMinimumHeight(35); // 增加按钮高度
    
    auto *clearBtn = new QPushButton(tr("清空"), this);
    clearBtn->setObjectName("secondary"); // 用于样式表
    clearBtn->setIcon(EmojiIcon::get("🧹"));
    clearBtn->setMinimumHeight(35);

    btnLayout->addWidget(execBtn);
    btnLayout->addWidget(clearBtn);
    
    layout->addLayout(btnLayout);

    connect(execBtn, &QPushButton::clicked, this, &ParameterForm::executeRequested);
    connect(clearBtn, &QPushButton::clicked, this, &ParameterForm::clear);
}

void ParameterForm::setCommand(const stdiolink::meta::CommandMeta *cmd)
{
    m_command = cmd;
    buildForm();
}

void ParameterForm::clear()
{
    for (auto &info : m_widgets) {
        if (info.setValue) {
            info.setValue(QJsonValue());
        }
    }
}

QJsonObject ParameterForm::collectData() const
{
    QJsonObject data;
    for (int i = 0; i < m_fields.size() && i < m_widgets.size(); ++i) {
        const auto &field = m_fields[i];
        const auto &info = m_widgets[i];
        if (info.getValue) {
            QJsonValue val = info.getValue();
            // 只跳过 null 值和空字符串（对于字符串类型）
            if (val.isNull()) continue;
            if (val.isString() && val.toString().isEmpty()) continue;
            data[field.name] = val;
        }
    }
    return data;
}

bool ParameterForm::validate() const
{
    for (const auto &info : m_widgets) {
        if (info.validate && !info.validate()) {
            return false;
        }
    }
    return true;
}

void ParameterForm::buildForm()
{
    // 清除旧控件
    while (m_formLayout->count() > 0) {
        auto *item = m_formLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_widgets.clear();
    m_fields.clear();

    if (!m_command) return;

    m_fields = m_command->params;
    for (const auto &field : m_fields) {
        auto info = FormWidgetFactory::createWidget(field, this);
        m_formLayout->addRow(info.label, info.widget);
        m_widgets.append(info);
    }
}
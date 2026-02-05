#include "parameter_form.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QApplication>
#include <QStyle>
#include <QLineEdit>
#include <QLabel>
#include <QClipboard>
#include <QFileInfo>
#include <QJsonDocument>
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

    // 命令行示例区域
    auto *cmdLineGroup = new QGroupBox(tr("命令行调用示例"), this);
    auto *cmdLineLayout = new QHBoxLayout(cmdLineGroup);
    cmdLineLayout->setContentsMargins(8, 8, 8, 8);

    m_cmdLineEdit = new QLineEdit(this);
    m_cmdLineEdit->setReadOnly(true);
    m_cmdLineEdit->setPlaceholderText(tr("选择命令后显示调用示例"));
    m_cmdLineEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: #2d2d2d; "
        "  color: #e0e0e0; "
        "  font-family: Consolas, 'Courier New', monospace; "
        "  font-size: 12px; "
        "  padding: 6px; "
        "  border: 1px solid #444; "
        "  border-radius: 4px; "
        "}"
    );

    auto *copyBtn = new QPushButton(EmojiIcon::get("📋"), tr("复制"), this);
    copyBtn->setFixedWidth(70);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_cmdLineEdit->text());
    });

    cmdLineLayout->addWidget(m_cmdLineEdit);
    cmdLineLayout->addWidget(copyBtn);
    layout->addWidget(cmdLineGroup);

    // 执行按钮区域
    auto *btnLayout = new QHBoxLayout;
    auto *execBtn = new QPushButton(tr("执行"), this);
    execBtn->setIcon(EmojiIcon::get("⚡"));
    execBtn->setMinimumHeight(35);

    auto *clearBtn = new QPushButton(tr("清空"), this);
    clearBtn->setObjectName("secondary");
    clearBtn->setIcon(EmojiIcon::get("🧹"));
    clearBtn->setMinimumHeight(35);

    btnLayout->addWidget(execBtn);
    btnLayout->addWidget(clearBtn);

    layout->addLayout(btnLayout);

    connect(execBtn, &QPushButton::clicked, this, &ParameterForm::executeRequested);
    connect(clearBtn, &QPushButton::clicked, this, &ParameterForm::clear);
}

void ParameterForm::setDriverProgram(const QString &program)
{
    m_driverProgram = program;
    updateCommandLineExample();
}

void ParameterForm::setCommand(const stdiolink::meta::CommandMeta *cmd)
{
    m_command = cmd;
    buildForm();
    updateCommandLineExample();
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

        // 连接值变化信号以更新命令行示例
        if (info.widget) {
            // 尝试连接常见的值变化信号
            if (auto *lineEdit = qobject_cast<QLineEdit*>(info.widget)) {
                connect(lineEdit, &QLineEdit::textChanged,
                        this, &ParameterForm::updateCommandLineExample);
            }
        }
    }
}

QString ParameterForm::escapeShellArg(const QString &arg) const
{
    // 如果包含空格或特殊字符，用双引号包裹
    if (arg.contains(' ') || arg.contains('"') || arg.contains('&') ||
        arg.contains('|') || arg.contains('<') || arg.contains('>')) {
        QString escaped = arg;
        escaped.replace("\"", "\\\"");
        return "\"" + escaped + "\"";
    }
    return arg;
}

void ParameterForm::updateCommandLineExample()
{
    if (m_driverProgram.isEmpty() || !m_command) {
        m_cmdLineEdit->clear();
        return;
    }

    QStringList parts;

    // Driver 程序名（只取文件名）
    QFileInfo fi(m_driverProgram);
    parts << fi.fileName();

    // 命令名
    parts << QString("--cmd=%1").arg(m_command->name);

    // 收集当前参数值
    QJsonObject data = collectData();
    for (auto it = data.begin(); it != data.end(); ++it) {
        QString key = it.key();
        QJsonValue val = it.value();

        QString valStr;
        if (val.isBool()) {
            valStr = val.toBool() ? "true" : "false";
        } else if (val.isDouble()) {
            double d = val.toDouble();
            if (d == static_cast<int>(d)) {
                valStr = QString::number(static_cast<int>(d));
            } else {
                valStr = QString::number(d);
            }
        } else if (val.isString()) {
            valStr = escapeShellArg(val.toString());
        } else if (val.isObject() || val.isArray()) {
            QJsonDocument doc;
            if (val.isObject()) {
                doc.setObject(val.toObject());
            } else {
                doc.setArray(val.toArray());
            }
            valStr = escapeShellArg(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        } else {
            continue;
        }

        parts << QString("--%1=%2").arg(key, valStr);
    }

    m_cmdLineEdit->setText(parts.join(" "));
}
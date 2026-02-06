#include "array_editor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>

ArrayEditor::ArrayEditor(stdiolink::meta::FieldMeta field, QWidget *parent)
    : QWidget(parent)
    , m_field(std::move(field))
    , m_list(new QListWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list->setMaximumHeight(100);
    layout->addWidget(m_list);

    auto *btnLayout = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("+"), this);
    auto *removeBtn = new QPushButton(tr("-"), this);
    //addBtn->setMaximumWidth(30);
    //removeBtn->setMaximumWidth(30);

    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(addBtn, &QPushButton::clicked, this, &ArrayEditor::addItem);
    connect(removeBtn, &QPushButton::clicked, this, &ArrayEditor::removeItem);
}

QJsonValue ArrayEditor::value() const
{
    QJsonArray arr;
    for (int i = 0; i < m_list->count(); ++i) {
        arr.append(m_list->item(i)->text());
    }
    return arr;
}

void ArrayEditor::setValue(const QJsonArray &arr)
{
    m_list->clear();
    for (const auto &val : arr) {
        m_list->addItem(val.toString());
    }
}

void ArrayEditor::addItem()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Add Item"),
                                         tr("Value:"), QLineEdit::Normal,
                                         QString(), &ok);
    if (ok && !text.isEmpty()) {
        m_list->addItem(text);
        emit valueChanged();
    }
}

void ArrayEditor::removeItem()
{
    auto *item = m_list->currentItem();
    if (item) {
        delete m_list->takeItem(m_list->row(item));
        emit valueChanged();
    }
}

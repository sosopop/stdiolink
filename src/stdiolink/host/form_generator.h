#pragma once

#include "stdiolink/protocol/meta_types.h"

#include <QJsonArray>
#include <QJsonObject>

namespace stdiolink {

/**
 * 表单描述
 */
struct FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;
};

/**
 * UI 描述生成器
 * 从元数据生成 UI 控件描述
 */
class UiGenerator {
public:
    static FormDesc generateCommandForm(const meta::CommandMeta& cmd);
    static FormDesc generateConfigForm(const meta::ConfigSchema& config);
    static QJsonObject toJson(const FormDesc& form);

private:
    static QJsonObject fieldToWidget(const meta::FieldMeta& field);
    static QString defaultWidget(meta::FieldType type);
};

} // namespace stdiolink

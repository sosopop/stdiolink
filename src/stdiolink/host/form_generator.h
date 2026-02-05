#pragma once

#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

namespace stdiolink {

/**
 * 表单描述
 */
struct STDIOLINK_API FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;
};

/**
 * UI 描述生成器
 * 从元数据生成 UI 控件描述
 */
class STDIOLINK_API UiGenerator {
public:
    static FormDesc generateCommandForm(const meta::CommandMeta& cmd);
    static FormDesc generateConfigForm(const meta::ConfigSchema& config);
    static QJsonObject toJson(const FormDesc& form);

    // M16: 高级 UI 生成
    static QHash<QString, QVector<meta::FieldMeta>> groupFields(
        const QVector<meta::FieldMeta>& fields);
    static QVector<meta::FieldMeta> sortFields(const QVector<meta::FieldMeta>& fields);

private:
    static QJsonObject fieldToWidget(const meta::FieldMeta& field);
    static QString defaultWidget(meta::FieldType type);
};

/**
 * 条件求值器 (M16)
 * 用于解析和求值 visibleIf 条件表达式
 */
class STDIOLINK_API ConditionEvaluator {
public:
    /**
     * 求值条件表达式
     * @param condition 条件表达式，如 "mode == 'advanced'"
     * @param context 上下文数据
     * @return 条件是否满足
     */
    static bool evaluate(const QString& condition, const QJsonObject& context);

private:
    static bool evaluateComparison(const QString& left, const QString& op,
                                   const QString& right, const QJsonObject& context);
    static QJsonValue resolveValue(const QString& expr, const QJsonObject& context);
};

} // namespace stdiolink

#pragma once

#include <QJsonObject>
#include "service_config_schema.h"
#include "stdiolink/protocol/meta_validator.h"

namespace stdiolink_service {

using stdiolink::meta::ValidationResult;

enum class UnknownFieldPolicy {
    Reject,
    Allow
};

class ServiceConfigValidator {
public:
    /// 合并配置源并校验（cli > file > defaults）
    static ValidationResult mergeAndValidate(
        const ServiceConfigSchema& schema,
        const QJsonObject& fileConfig,
        const QJsonObject& rawCliConfig,
        UnknownFieldPolicy unknownFieldPolicy,
        QJsonObject& mergedOut);

    /// 校验配置对象是否符合 schema
    static ValidationResult validate(
        const ServiceConfigSchema& schema,
        const QJsonObject& config);

    /// 用 schema 默认值填充缺失字段
    static QJsonObject fillDefaults(
        const ServiceConfigSchema& schema,
        const QJsonObject& config);

private:
    static QJsonObject deepMerge(const QJsonObject& base, const QJsonObject& override);
    static QJsonObject convertRawValues(const ServiceConfigSchema& schema,
                                        const QJsonObject& raw);
    static ValidationResult rejectUnknownFields(const ServiceConfigSchema& schema,
                                                const QJsonObject& config,
                                                const QString& prefix = QString());
};

} // namespace stdiolink_service

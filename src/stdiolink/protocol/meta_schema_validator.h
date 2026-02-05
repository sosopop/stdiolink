#pragma once

#include "stdiolink/stdiolink_export.h"
#include "meta_types.h"
#include <QString>

namespace stdiolink {

/**
 * 元数据结构校验器 (M18)
 */
class STDIOLINK_API MetaSchemaValidator {
public:
    /**
     * 验证元数据结构
     * @param meta 元数据
     * @param error 错误信息输出
     * @return 是否有效
     */
    static bool validate(const meta::DriverMeta& meta, QString* error = nullptr);

private:
    static bool validateSchemaVersion(const QString& version, QString* error);
    static bool validateDriverInfo(const meta::DriverInfo& info, QString* error);
    static bool validateCommands(const QVector<meta::CommandMeta>& commands, QString* error);
    static bool validateCommand(const meta::CommandMeta& cmd, QString* error);
};

} // namespace stdiolink

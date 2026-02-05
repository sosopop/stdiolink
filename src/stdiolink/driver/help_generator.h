#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QString>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * CLI 帮助生成器
 */
class STDIOLINK_API HelpGenerator {
public:
    /**
     * 生成版本信息
     */
    static QString generateVersion(const meta::DriverMeta& meta);

    /**
     * 生成全局帮助
     */
    static QString generateHelp(const meta::DriverMeta& meta);

    /**
     * 生成命令详情帮助
     */
    static QString generateCommandHelp(const meta::CommandMeta& cmd);

    /**
     * 格式化参数
     */
    static QString formatParam(const meta::FieldMeta& field);

    /**
     * 生成系统选项帮助 (M20)
     */
    static QString generateSystemOptions();

private:
    static QString formatConstraints(const meta::Constraints& c);
    static QString fieldTypeToString(meta::FieldType type);
};

} // namespace stdiolink

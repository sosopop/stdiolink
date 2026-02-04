#pragma once

#include <QByteArray>
#include <QString>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * 元数据导出器
 */
class MetaExporter {
public:
    /**
     * 导出为 JSON 字节数组
     * @param meta 元数据
     * @param pretty 是否格式化输出
     */
    static QByteArray exportJson(const meta::DriverMeta& meta, bool pretty = true);

    /**
     * 导出到文件
     * @param meta 元数据
     * @param path 文件路径
     * @return 是否成功
     */
    static bool exportToFile(const meta::DriverMeta& meta, const QString& path);
};

} // namespace stdiolink

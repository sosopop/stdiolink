#pragma once

#include <QString>
#include <QStringList>

namespace stdiolink_service {

struct DriverResolveResult {
    QString path;                // 成功时为绝对路径，失败时为空
    QStringList searchedPaths;   // 已尝试的位置列表（用于错误信息）
};

DriverResolveResult resolveDriverPath(const QString &driverName,
                                      const QString &dataRoot,
                                      const QString &appDir);

} // namespace stdiolink_service

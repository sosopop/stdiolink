#pragma once

#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace stdiolink {

/**
 * Driver 配置 (M19)
 */
struct STDIOLINK_API DriverConfig {
    QString id;
    QString program;
    QStringList args;
    std::shared_ptr<meta::DriverMeta> meta;
    QString metaHash;
};

/**
 * Driver 注册中心 (M19)
 * 单例模式，管理所有已注册的 Driver
 */
class STDIOLINK_API DriverRegistry {
public:
    static DriverRegistry& instance();

    // 禁止拷贝
    DriverRegistry(const DriverRegistry&) = delete;
    DriverRegistry& operator=(const DriverRegistry&) = delete;

    /**
     * 注册 Driver
     */
    void registerDriver(const QString& id, const DriverConfig& config);

    /**
     * 注销 Driver
     */
    void unregisterDriver(const QString& id);

    /**
     * 获取已注册的 Driver 列表
     */
    QStringList listDrivers() const;

    /**
     * 获取 Driver 配置
     */
    DriverConfig getConfig(const QString& id) const;

    /**
     * 检查 Driver 是否已注册
     */
    bool hasDriver(const QString& id) const;

    /**
     * 健康检查单个 Driver
     * @return 是否健康
     */
    bool healthCheck(const QString& id);

    /**
     * 批量健康检查所有 Driver
     */
    void healthCheckAll();

    /**
     * 扫描目录发现 Driver
     * 查找 driver.meta.json 文件
     */
    void scanDirectory(const QString& path);

    /**
     * 清空所有注册（测试用）
     */
    void clear();

private:
    DriverRegistry() = default;

    bool loadMetaFromFile(const QString& path, DriverConfig& config);
    QString computeMetaHash(const QByteArray& data);

    QHash<QString, DriverConfig> m_drivers;
};

} // namespace stdiolink

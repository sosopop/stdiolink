#pragma once

#include "stdiolink/protocol/meta_types.h"

#include <QHash>
#include <QMutex>
#include <memory>

namespace stdiolink {

/**
 * 元数据缓存
 * 按 Driver ID 缓存元数据，支持变更检测
 */
class MetaCache {
public:
    static MetaCache& instance();

    void store(const QString& driverId, std::shared_ptr<meta::DriverMeta> meta);
    void store(const QString& driverId,
               std::shared_ptr<meta::DriverMeta> meta,
               const QString& metaHash);

    std::shared_ptr<meta::DriverMeta> get(const QString& driverId) const;

    void invalidate(const QString& driverId);

    void clear();

    bool hasChanged(const QString& driverId, const QString& metaHash) const;

private:
    MetaCache() = default;
    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<meta::DriverMeta>> m_cache;
    QHash<QString, QString> m_hashCache;
};

} // namespace stdiolink

#include "meta_cache.h"

namespace stdiolink {

MetaCache& MetaCache::instance() {
    static MetaCache cache;
    return cache;
}

void MetaCache::store(const QString& driverId, std::shared_ptr<meta::DriverMeta> meta) {
    QMutexLocker locker(&m_mutex);
    m_cache[driverId] = meta;
}

void MetaCache::store(const QString& driverId,
                      std::shared_ptr<meta::DriverMeta> meta,
                      const QString& metaHash) {
    QMutexLocker locker(&m_mutex);
    m_cache[driverId] = meta;
    m_hashCache[driverId] = metaHash;
}

std::shared_ptr<meta::DriverMeta> MetaCache::get(const QString& driverId) const {
    QMutexLocker locker(&m_mutex);
    return m_cache.value(driverId);
}

void MetaCache::invalidate(const QString& driverId) {
    QMutexLocker locker(&m_mutex);
    m_cache.remove(driverId);
    m_hashCache.remove(driverId);
}

void MetaCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_hashCache.clear();
}

bool MetaCache::hasChanged(const QString& driverId, const QString& metaHash) const {
    QMutexLocker locker(&m_mutex);
    if (!m_hashCache.contains(driverId)) {
        return true;
    }
    return m_hashCache[driverId] != metaHash;
}

} // namespace stdiolink

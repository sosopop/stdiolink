# 里程碑 19：Host 注册中心

## 1. 目标

建立完整的 Host 注册闭环，实现"即插即用"。

## 2. 对应需求

- **需求3**: Host 侧注册与索引体系 (Registry & Discovery)

## 3. DriverRegistry 设计

```cpp
class DriverRegistry {
public:
    static DriverRegistry& instance();

    void registerDriver(const QString& id, const DriverConfig& config);
    void unregisterDriver(const QString& id);

    QStringList listDrivers() const;
    DriverConfig getConfig(const QString& id) const;

    bool healthCheck(const QString& id);
    void healthCheckAll();
};
```

## 4. DriverConfig 结构

```cpp
struct DriverConfig {
    QString id;
    QString program;
    QStringList args;
    std::shared_ptr<meta::DriverMeta> meta;
};
```

## 5. 验收标准

1. 支持多 Driver 注册/注销
2. 统一查询接口
3. 健康检查机制
4. 元数据缓存集成

## 6. 依赖关系

- **前置**: M18 (版本协商)
- **后续**: 无（最终里程碑）

# 里程碑 18：元数据版本协商

## 1. 目标

引入运行时与编译时的 Meta 质量保证。

## 2. 对应需求

- **需求7**: 元数据校验与版本协商 (Schema Validation & Versioning)

## 3. 版本协商机制

```cpp
struct DriverMeta {
    QString schemaVersion = "1.0";  // 元数据版本
};
```

## 4. Host 端版本检查

```cpp
class MetaVersionChecker {
public:
    static bool isCompatible(const QString& hostVersion,
                             const QString& driverVersion);
    static QStringList getSupportedVersions();
};
```

## 5. 依赖关系

- **前置**: M17 (配置注入闭环)
- **后续**: M19 (Host注册中心)

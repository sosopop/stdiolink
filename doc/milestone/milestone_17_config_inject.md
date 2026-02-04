# 里程碑 17：配置注入闭环

## 1. 目标

实现从"UI 保存"到"驱动生效"的完整链路。

## 2. 对应需求

- **需求5**: 配置注入与生效闭环 (Config Injected Loop)

## 3. 注入策略

| 方式 | 说明 |
|------|------|
| env | 环境变量注入 |
| args | 启动参数注入 |
| file | 配置文件注入 |
| command | 运行时命令注入 |

## 4. ConfigApply 结构

```cpp
struct ConfigApply {
    QString method;     // env|args|file|command
    QString envPrefix;  // 环境变量前缀
    QString command;    // 配置命令名
    QString fileName;   // 配置文件名
};
```

## 5. 热重载机制

```cpp
// meta.config.set 命令
// meta.config.reload 命令
```

## 6. 依赖关系

- **前置**: M16 (高级UI生成)
- **后续**: M18 (版本协商)

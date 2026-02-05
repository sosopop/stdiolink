# 最佳实践

本章介绍使用 stdiolink 的最佳实践。

## Driver 开发

### 使用元数据系统

推荐使用 `IMetaCommandHandler` 而非 `ICommandHandler`：

- 自动参数验证
- 自动生成帮助文档
- 支持 Console 模式

### 合理设置 Profile

- **OneShot**：适合一次性任务
- **KeepAlive**：适合需要多次调用的场景

### 错误处理

使用标准错误码：

```cpp
resp.error(400, QJsonObject{{"message", "invalid params"}});
resp.error(404, QJsonObject{{"message", "unknown command"}});
resp.error(500, QJsonObject{{"message", "internal error"}});
```

## Host 开发

### 设置合理的超时

```cpp
Message msg;
if (!t.waitNext(msg, 5000)) {
    // 处理超时
}
```

### 及时终止 Driver

```cpp
Driver d;
d.start("driver.exe");
// ... 使用 ...
d.terminate();  // 不要忘记
```

## 性能优化

### 复用 Driver 进程

使用 KeepAlive 模式复用进程，避免频繁启动。

### 并行处理

使用 `waitAnyNext` 并行处理多个任务。

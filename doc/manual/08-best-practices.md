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

## JS Service 开发

### 优先使用 openDriver()

推荐使用 `openDriver()` Proxy 而非直接操作 `Driver`/`Task`：

```js
// 推荐
const calc = await openDriver('./stdio.drv.calculator');
const result = await calc.add({ a: 1, b: 2 });
calc.$close();
```

### 使用 config.schema.json 管理配置

将外部参数通过配置系统管理，而非硬编码：

`config.schema.json`：
```json
{
    "driverPath": { "type": "string", "required": true },
    "timeout": { "type": "int", "default": 5000 }
}
```

`index.js`：
```js
const config = getConfig();
```

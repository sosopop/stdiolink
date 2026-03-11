# 最佳实践

本章介绍使用 stdiolink 的最佳实践。

## Driver 开发

### 使用元数据系统

推荐使用 `IMetaCommandHandler` 而非 `ICommandHandler`：

- 自动参数验证
- 自动生成帮助文档
- 支持 Console 模式

### Console 参数建议

- 优先使用显式路径写法：`--units[0].id=1 --units[0].size=10000`
- 对纯数字字符串、枚举字符串使用 Canonical 字面量：`--password="123456" --mode_code="1"`
- 对特殊键名使用 `labels["app.kubernetes.io/name"]="demo"`
- 不要依赖整段 JSON 在不同 shell 中的转义行为

### 不推荐的写法

```bash
--units=[{"id":1,"size":10000}]
--a={"b":1} --a.c=2
```

### 选择合适的生命周期

- **OneShot**：适合一次性任务（默认模式）
- **KeepAlive**：适合需要多次调用的场景，通过 `--profile=keepalive` 指定

### 统一 Driver 可执行文件命名（Server 场景）

- 若需要被 `stdiolink_server` 扫描，建议可执行文件 basename 使用 `stdio.drv.` 前缀
- 例如：`stdio.drv.modbustcp`、`stdio.drv.calculator`
- 仅 Host 直连（手动 `Driver::start(path)`）场景可不强制此命名

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
d.terminate();
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

`openDriver()` 固定按 keepalive 方式启动；如果需要自己控制 `--profile=oneshot|keepalive`，改用 `new Driver()`。

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

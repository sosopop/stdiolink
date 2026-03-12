# 故障排除

本章介绍常见问题及解决方案。

## 启动问题

### Driver 启动失败

**症状：** `start()` 返回 `false`

**可能原因：**
- 可执行文件路径错误
- 缺少依赖库
- 权限不足

**解决方案：**
```cpp
if (!d.start(path)) {
    qDebug() << "Error:" << d.process()->errorString();
}
```

### 进程立即退出

**可能原因：**
- Driver 内部错误
- Profile 设置为 OneShot

**解决方案：**
检查 Driver 的 stderr 输出。

## 通信问题

### 响应超时

**症状：** `waitNext()` 返回 `false`

**可能原因：**
- Driver 处理时间过长
- Driver 崩溃
- 协议格式错误

**解决方案：**
增加超时时间或检查 Driver 状态。

### 数据解析失败

**可能原因：**
- JSON 格式错误
- 编码问题

**解决方案：**
确保使用 UTF-8 编码。

## Console 参数问题

### `password: expected string`

**症状：** console 调用返回 `ValidationFailed`，消息包含 `password: expected string`

**根因：** 旧实现会把 `--password=123456` 这类纯数字字符串提前推断成 number；M98 后 console 模式会按元数据保留字符串，但 stdio 请求仍必须传 JSON string。

**推荐写法：**

```bash
stdio.drv.example --cmd=login --password="123456"
```

对于 `stdiolink_service --config.*`，现在也会按 `config.schema.json` 的字段类型先解析：

```bash
stdiolink_service ./my_service --config.vision.password=123456
```

如果 `vision.password` 在 schema 中是 `string`，上面的写法会保留为字符串，不再误判成 number。

### `units: expected array` / `expected object`

**症状：** 在 PowerShell 5.1、cmd 中传 `--units=[{"id":1}]` 或复杂对象参数时失败，错误消息包含 `expected array` 或 `expected object`

**根因：** 这类写法依赖 shell 保留 JSON 内部双引号；PowerShell 5.1 和 cmd 对这类转义尤其脆弱。

**推荐写法：**

```bash
--units[0].id=1 --units[0].size=10000
```

`stdiolink_service --config.*` 也一样：路径叶子 override 推荐写成 `--config.units[0].id=1`，不要依赖 `--config.units=[{"id":1}]` 这种完整 JSON 在 shell 中被原样保留。

### PowerShell 5.1 / cmd 兼容性

- `PowerShell 7` 对复杂参数保留更稳定
- `PowerShell 5.1` 和 `cmd` 更容易在传参过程中破坏 JSON 引号
- 对第三方脚本集成，优先传 argv token 列表，不要手工拼一整行 JSON 命令

## 迁移说明

| 旧写法 | 失败症状 | 推荐替代 |
|--------|----------|----------|
| `--units=[{"id":1}]` | `expected array` | `--units[0].id=1` |
| `--password=123456` | `expected string` | `--password="123456"` |
| `--a={"b":1} --a.c=2` | `path conflict` / 校验失败 | 统一使用完整 JSON 或统一使用子路径 |

补充：
- `stdiolink_service --config.password=123456` 现在在 schema 字段为 `string/enum` 时会像 driver console 一样保留字符串。
- `stdiolink_service --config.obj={"k":1}` 这类完整容器 literal 仍要求 shell 正确保留 JSON 引号；在 PowerShell 5.1 / cmd 下依旧更脆弱。

## JS Service 问题

### 模块加载失败

**症状：** `ReferenceError: Module not found`

**可能原因：**
- 模块文件路径错误
- 相对路径基准目录不正确

**解决方案：**
检查 import 路径是否正确，相对路径基于当前文件所在目录解析。

### 配置校验失败

**症状：** 启动时 stderr 报配置校验错误，退出码 1

**可能原因：**
- 必填字段未通过 `--config.key=value` 提供
- 值类型与 schema 声明不匹配
- 值超出约束范围

**解决方案：**
```bash
# 查看服务的配置项帮助
stdiolink_service ./my_service --help

# 导出 schema 检查字段定义
stdiolink_service ./my_service --dump-config-schema
```

### openDriver() 启动失败

**症状：** `Error: Failed to start driver`

**可能原因：**
- Driver 可执行文件路径错误
- Driver 缺少依赖库

**解决方案：**
先在终端直接运行 Driver 确认可正常启动。

补充：
- `openDriver()` 现在固定使用 keepalive；不要再传 `profilePolicy` 之类旧选项。
- 如果 Driver 启动后很快退出，后续通常会通过 `Task` / `waitAny()` 收到 `error`，而不是长期静默返回 `null`。

### DriverBusyError

**症状：** `DriverBusyError: request already in flight`

**原因：** 同一 Driver 实例上有未完成的请求时发起了新请求。

**解决方案：**
使用 `await` 等待前一个请求完成后再发起新请求，或使用不同的 Driver 实例并行调用。

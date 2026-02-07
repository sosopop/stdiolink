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

## JS Service 问题

### 模块加载失败

**症状：** `ReferenceError: Module not found`

**可能原因：**
- 模块文件路径错误
- 相对路径基准目录不正确

**解决方案：**
检查 import 路径是否正确，相对路径基于当前文件所在目录解析。

### 配置校验失败

**症状：** `defineConfig()` 抛出异常

**可能原因：**
- 必填字段未通过 `--config.key=value` 提供
- 值类型与 schema 声明不匹配
- 值超出约束范围

**解决方案：**
```bash
# 查看脚本的配置项帮助
stdiolink_service script.js --help

# 导出 schema 检查字段定义
stdiolink_service script.js --dump-config-schema
```

### openDriver() 启动失败

**症状：** `Error: Failed to start driver`

**可能原因：**
- Driver 可执行文件路径错误
- Driver 缺少依赖库

**解决方案：**
先在终端直接运行 Driver 确认可正常启动。

### DriverBusyError

**症状：** `DriverBusyError: request already in flight`

**原因：** 同一 Driver 实例上有未完成的请求时发起了新请求。

**解决方案：**
使用 `await` 等待前一个请求完成后再发起新请求，或使用不同的 Driver 实例并行调用。

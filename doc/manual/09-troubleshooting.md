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

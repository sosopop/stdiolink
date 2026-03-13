# Add Driver

## Goal

新增一个可被 Host/Service/Server/WebUI 消费的 C++ Driver。

## Steps

1. 在 `src/drivers/driver_<name>/` 建目录，提供 `main.cpp`、handler、`CMakeLists.txt`
2. `main.cpp` 中创建 `DriverCore` 并注册 handler
3. 若需要文档/表单/DriverLab，handler 实现 `IMetaCommandHandler`
4. `CMakeLists.txt` 设置输出名 `stdio.drv.<name>` 并注册 runtime 组装；需要默认发布时放在 `src/drivers/`，`src/demo/` 下示例 Driver 默认不进入 runtime
5. 构建后确认产物进入 `build/runtime_*/data_root/drivers/<dir>/`
6. 如需 Server/WebUI 消费，同时验证 `meta.describe` 和 `--export-meta=<path>`；Server 扫描实际消费的是导出的 `driver.meta.json`
7. 补 GTest；新增里程碑能力时补 Smoke

## Must Check

- 命令参数是否有元数据
- 生命周期是 `OneShot` 还是 `KeepAlive`
- `OneShot` Driver 如果每条命令显式带连接参数，要写清默认值来源、是否跨命令记忆状态、哪些命令允许依赖最近一次执行上下文
- Windows 管道读取是否仍走 Qt 行读取
- 错误是否通过 `resp.error()` 结构化返回

## Main Source Entry

- `src/stdiolink/driver/`
- `src/drivers/`
- `src/tests/test_driver_core.cpp`

## Related

- `../02-driver/driver-lifecycle.md`
- `../02-driver/driver-meta.md`
- `../07-testing-build/test-matrix.md`

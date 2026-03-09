# Add Driver

## Goal

新增一个可被 Host/Service/Server/WebUI 消费的 C++ Driver。

## Steps

1. 在 `src/drivers/driver_<name>/` 建目录，提供 `main.cpp`、handler、`CMakeLists.txt`
2. `main.cpp` 中创建 `DriverCore` 并注册 handler
3. 若需要文档/表单/DriverLab，handler 实现 `IMetaCommandHandler`
4. `CMakeLists.txt` 设置输出名 `stdio.drv.<name>` 并注册 runtime 组装
5. 构建后确认产物进入 `build/runtime_*/data_root/drivers/<dir>/`
6. 如需 Server/WebUI 消费，验证 `meta.describe` 输出
7. 补 GTest；新增里程碑能力时补 Smoke

## Must Check

- 命令参数是否有元数据
- 生命周期是 `OneShot` 还是 `KeepAlive`
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

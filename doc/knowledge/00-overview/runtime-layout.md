# Runtime Layout

## Purpose

定位可执行文件、`data_root`、发布包和本地调试入口。

## Key Conclusions

- 构建产物分 raw 和 runtime 两层：`build/<config>/` 仅编译输出，`build/runtime_<config>/` 才是可运行目录。
- Driver 在 runtime 中按子目录分发：`data_root/drivers/<driver-dir>/<exe>`。
- Service 和 Project 的事实源在 `data_root/services/`、`data_root/projects/`。
- `resolveDriver()`、Server 扫描器、发布包都依赖 runtime 同构布局。

## Layout

- `build/runtime_debug/bin/`：核心二进制、Qt 依赖。
- `build/runtime_debug/data_root/drivers/`：Driver 可执行文件目录。
- `build/runtime_debug/data_root/services/`：Service 模板目录。
- `build/runtime_debug/data_root/projects/`：Project 配置目录。
- `release/<pkg>/`：发布包根，结构与 runtime 基本同构。

## Modify Entry

- 构建与组装：根目录 `build.bat` / `build.sh`，以及 CMake `assemble_runtime`
- 发布：`tools/publish_release.ps1` / `tools/publish_release.sh`
- Driver 查找：`src/stdiolink_service/bindings/js_driver_resolve*`
- Server 启动参数：`src/stdiolink_server/config/server_args.*`

## Risks

- 只编译 raw 目录、不组装 runtime 时，Driver/Service/Server 运行会缺少相对路径资源。
- Windows 直接运行 Driver 前需把 `build/runtime_debug/bin` 放入 `PATH`。

## Related

- `../04-service/service-config-and-driver.md`
- `../05-server/server-lifecycle.md`
- `../07-testing-build/build-release.md`

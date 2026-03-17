# Build And Release

## Purpose

统一构建、运行时组装、发布打包和直接启动入口。

## Core Commands

- Windows 构建：`build.bat` / `build.bat Release`；内部包装到 `tools/release.py build`
- Unix 构建：`./build.sh` / `./build.sh Release`；内部包装到 `tools/release.py build`
- CTest：`ctest --test-dir build --output-on-failure`
- 跨平台构建/测试/发布：`tools/release.py`；便捷封装：`tools/release.ps1`、`tools/release.bat`、`tools/release.sh`

## Key Conclusions

- 运行联调优先看 `build/runtime_debug/`
- 发布验证优先看 `release/<pkg>/`
- `build/runtime_*` 默认没有发布包根脚本；Windows 模板在 `tools/release_scripts/`，Unix 模板在 `tools/release_scripts_unix/`，由 `tools/release.py publish` 直接拷贝
- runtime / 发布包里的 Driver 默认只来自 `src/drivers/`；`src/demo/` 下示例 Driver 会编译，但不会自动发布到 `data_root/drivers/`
- runtime / 发布包里的 Service、Project 模板默认只来自 `src/data_root/`；`src/demo/server_manager_demo/data_root/` 仅补充 demo 配置、日志、工作区与脚本
- 测试构建会额外把 `stdio.drv.calculator` 注入 `runtime_*` 作为测试资产；这一步仅服务 GTest/JS 集成测试，不属于默认发布内容
- `tools/release.py publish` 发布前默认会运行完整测试链；不会自动把独立注册的 `bin_scan_orchestrator_service_tests` 打进发布包
- `tools/release.py publish` 在复制 `runtime_release/bin` 时会始终排除 `bin_scan_orchestrator_service_tests`；即使传了 `--with-tests`，该二进制也不会进入发布包
- `tools/release.py publish` 会在包内联执行重复组件校验：`bin/` 不允许出现 `stdio.drv.*`，`data_root/drivers/` 下同名 `stdio.drv.*` 不允许分布在多个子目录
- `tools/release.py build` 现在直接承载 CMake/vcpkg 的跨平台构建流程；根目录 `build.bat` / `build.sh` 只保留旧命令兼容包装
- `tools/release.py test` 和 `tools/release.py publish` 运行前都会先检查 6200 / 3000 端口，以及残留的 `stdiolink_server`、`stdiolink_service`、`stdio.drv*` 进程；若发现占用会直接告警并退出，并尽量给出端口对应的 PID / 进程名
- `tools/release.py test` 会在跑测试前先调用内置构建流程重新编译，默认按 `release` 配置构建并执行测试，避免误用旧产物；任一测试套件失败后会立即终止，不继续执行后续套件
- Windows 发布包里的 `dev.bat` / `dev.ps1` 会在启动时扫描 `data_root/drivers/`；`dev.ps1` 还会按 `data_root/projects/*/config.json` 动态生成 project alias，并提供 `projects` 列表命令
- Unix 发布包里的 `dev.sh` 会打开带环境变量和 alias 的交互式 bash，并按 `data_root/drivers/`、`data_root/projects/*/config.json` 动态注册 Driver / Project 入口

## Modify Entry

- CMake 输出/组装：顶层 CMake 和 `assemble_runtime`
- 构建/测试/发布脚本：`tools/release.py`
- Server 启动参数：`src/stdiolink_server/config/server_args.*`

## Debug Entry

- Driver 单跑：先配 `PATH` 指到 `build/runtime_debug/bin`
- Server 单跑：`build/runtime_release/bin/stdiolink_server --data-root=... --webui-dir=...`

## Driver Standalone Minimal Steps

- Windows:
  `set PATH=%CD%\build\runtime_debug\bin;%PATH%`
  `build\runtime_debug\data_root\drivers\stdio.drv.<name>\stdio.drv.<name>.exe --export-meta`
- 先确认目标 Driver 已被组装进 `data_root/drivers/`，不要从 raw 输出目录直接联调
- 如果 Service 或 Server 找不到 Driver，优先回查 `runtime-layout.md` 和 `resolveDriver()` 路径链路

## Related

- `test-matrix.md`
- `../00-overview/runtime-layout.md`

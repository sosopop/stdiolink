# Build And Release

## Purpose

统一构建、运行时组装、发布打包和直接启动入口。

## Core Commands

- Windows 构建：`build.bat` / `build.bat Release`
- Unix 构建：`./build.sh` / `./build.sh Release`
- CTest：`ctest --test-dir build --output-on-failure`
- 发布：`tools/publish_release.ps1` / `tools/publish_release.sh`

## Key Conclusions

- 运行联调优先看 `build/runtime_debug/`
- 发布验证优先看 `release/<pkg>/`
- `build/runtime_*` 默认没有发布包根脚本；`dev.ps1`、`start.*` 来自发布脚本
- 发布包里的 `dev.ps1` 会按 `data_root/projects/*/config.json` 动态生成 project alias，并提供 `projects` 列表命令

## Modify Entry

- CMake 输出/组装：顶层 CMake 和 `assemble_runtime`
- 发布脚本：`tools/publish_release.*`
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

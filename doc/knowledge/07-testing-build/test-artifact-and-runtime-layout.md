# Test Artifact And Runtime Layout

## Purpose

说明构建目录、运行目录和发布目录的边界，避免把测试问题归因到错误目录。

## Key Paths

- `build/`：CMake/Ninja 产物和 CTest 注册信息，不等于可直接联调的运行目录。
- `build/runtime_release/`：默认可运行目录，包含 `bin/` 和 `data_root/`。
- `build/runtime_debug/`：显式调试 Debug 构建时使用的可运行目录。
- `build_vs/runtime_debug/`、`build_vs/runtime_release/`：Visual Studio 多配置构建的可运行目录。
- `release/<pkg>/`：发布包验证入口，不等于 `build/runtime_*`。

## Generator Differences

- Ninja 单配置：原始可执行文件在 `build/<config-lower>/`，runtime 在 `build/runtime_<config-lower>/bin`。
- Visual Studio 多配置：原始可执行文件在 `build_vs/<config-lower>/`，runtime 在 `build_vs/runtime_<config-lower>/bin`。

## Common Pitfalls

- 把 `build/`、`runtime_*` 和 `release/<pkg>/` 混成同一类目录。
- 在 debug 目录复现通过后，直接假设 release 目录也一样。
- 忽略运行目录里的 `data_root`、DLL、工作目录和资源组装。

## When To Go Deeper

- 想确认 `ctest` 到底运行了什么，转到 `ctest-vs-direct-run.md`。
- 想确认当前失败更像环境还是行为回归，转到 `triage-test-failure.md`。

## Related

- `build-release.md`
- `ctest-vs-direct-run.md`
- `verify-reported-failure.md`
- `test-matrix.md`

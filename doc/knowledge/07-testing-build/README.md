# Testing And Build

本目录覆盖本地构建、测试矩阵、发布打包和调试入口。

## Files

- `build-release.md`：构建命令、runtime 组装、发布包生成和运行入口。
- `test-matrix.md`：GTest、Smoke、Vitest、Playwright 的职责和新增功能测试要求。
- `verify-reported-failure.md`：从失败日志出发确认问题是否当前仍存在的最短流程。
- `choose-test-entry.md`：按改动类型和排查目标选择最省时测试入口。
- `triage-test-failure.md`：先分构建、运行目录、环境、并发、真实回归。
- `test-artifact-and-runtime-layout.md`：解释 `build/`、`runtime_*`、CTest 和直接运行测试程序的关系。

## Source Anchors

- `build.bat` / `build.sh`
- `tools/`
- `src/tests/`
- `src/smoke_tests/`
- `src/webui/`

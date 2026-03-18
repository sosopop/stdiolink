# CTest Vs Direct Run

## Purpose

说明 `ctest` 注册命令和手动运行测试程序的差异，避免把两者当成同一层。

## What CTest Knows

- `ctest` 只知道“注册测试命令”，不天然知道单个 GTest case。
- 当前仓库里，`stdiolink_tests` 常作为整体测试程序被 CTest 注册。

## Fast Checks

```powershell
ctest --test-dir build -N -V
Get-Content build\CTestTestfile.cmake
Get-Content build\src\CTestTestfile.cmake
ctest --test-dir build_vs -C Debug -N -V
```

## Decision Rules

- 要复现单个 GTest case，优先直接跑 `stdiolink_tests.exe --gtest_filter=...`。
- 直接运行时只允许使用 `runtime_<config>/bin/stdiolink_tests(.exe)`；程序启动前会检查自己是否处于 `bin/` + 同级 `data_root/{drivers,services}` 的 runtime 布局。
- 如果误跑 `build/release/stdiolink_tests` 或 `build/debug/stdiolink_tests` 这类 raw build 产物，测试程序会直接报错退出；不要把这种失败当成业务回归。
- `ctest` 和手动运行结果不一致时，先比较可执行文件路径、工作目录、环境变量和 debug/release 目录。
- 需要验证“当前注册命令整体是否通过”时再回到 `ctest`。

## Related

- `test-artifact-and-runtime-layout.md`
- `verify-reported-failure.md`
- `choose-test-entry.md`

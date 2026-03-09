# Test Artifact And Runtime Layout

## Purpose

说明构建目录、运行目录、CTest 注册命令和直接执行测试程序之间的关系，避免复现路径选错。

## Key Paths

- `build/`
  - CMake/Ninja 产物和 CTest 注册信息
- `build/runtime_debug/`
  - debug 运行目录，适合本地联调
- `build/runtime_release/`
  - release 运行目录，也是当前 CTest 常见的测试程序来源
- `release/<pkg>/`
  - 发布包验证入口，不等于开发构建目录

## What CTest Knows

- `ctest` 知道的是“注册测试命令”
- 该命令可能是：
  - `stdiolink_tests.exe`
  - `python src/smoke_tests/run_smoke.py --plan ...`
  - 其他脚本或可执行文件
- `ctest` 默认不知道 GTest case 级别名称，除非显式做了 `gtest_discover_tests`

## Inspect The Registered Commands

```powershell
ctest --test-dir build -N -V
Get-Content build\CTestTestfile.cmake
Get-Content build\src\CTestTestfile.cmake
```

## Direct Run vs CTest

- 直接运行测试程序
  - 适合 case 级复现和快速缩小范围
- 运行 `ctest`
  - 适合验证当前 CMake 注册命令是否整体通过
- 两者结果不一致时，优先比较：
  - 可执行文件路径
  - 工作目录
  - 环境变量
  - 使用的是 debug 还是 release 运行目录

## Typical Commands

### 直接跑 GTest

```powershell
build\runtime_release\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
build\runtime_debug\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
```

### 跑 CTest

```powershell
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "^stdiolink_tests$" --output-on-failure
ctest --test-dir build -L smoke --output-on-failure
```

## Common Pitfalls

- 认为 `ctest` 里的 `stdiolink_tests` 等于单个 GTest case
- 在 debug 目录复现通过，就默认 release 也通过
- 忽略测试程序需要的 `data_root/drivers`、`services`、DLL 和工作目录
- 把发布包问题放在 `build/runtime_*` 中验证，或反过来

## Related

- `build-release.md`
- `verify-reported-failure.md`
- `test-matrix.md`

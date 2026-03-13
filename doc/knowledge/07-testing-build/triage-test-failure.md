# Triage Test Failure

## Purpose

把测试失败先归类，再决定是查代码、查构建产物还是查环境。

## First Split

- 构建产物问题：测试程序不存在、`ctest` 指向错误产物、debug/release 混用。
- 运行目录问题：`data_root` 缺文件、driver/service 未进 `runtime_*`、发布包和 build 目录不一致。
- 环境问题：PATH 缺依赖 DLL、端口被占用、残留进程或临时文件污染。
- 时间与并发问题：超时过紧、异步轮询竞争、对系统时间或负载敏感。
- 真实行为回归：输入不变但断言稳定失败，且 debug/release 都能复现。

## Fast Checks

```powershell
ctest --test-dir build -N -V
Get-ChildItem build\runtime_release\bin
Get-ChildItem build\runtime_release\data_root
Get-Process | Where-Object { $_.ProcessName -like '*stdiolink*' }
netstat -ano | Select-String ":<port>"
```

## Decision Rules

- 没确认测试程序路径前，不下代码回归结论。
- 单 case 可过但套件失败时，先查运行目录和测试间污染，不要先改业务逻辑。
- 同一失败能在隔离环境稳定复现后，再进入源码排查。

## Related

- `ctest-vs-direct-run.md`
- `verify-reported-failure.md`
- `test-artifact-and-runtime-layout.md`
- `../08-workflows/debug-change-entry.md`

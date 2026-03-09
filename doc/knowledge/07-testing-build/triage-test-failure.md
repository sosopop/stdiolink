# Triage Test Failure

## Purpose

把测试失败先归类，再决定是查代码、查构建产物还是查环境。

## First Split

- 构建产物问题
  - 测试程序不存在
  - `ctest` 指向的不是预期产物
  - debug/release 产物混用
- 运行目录问题
  - `data_root` 缺文件
  - driver/service 未被组装进 `runtime_*`
  - 发布包和 build 目录内容不一致
- 环境问题
  - PATH 缺依赖 DLL
  - 端口被占用
  - 残留进程或临时文件污染
- 时间与并发问题
  - 超时过紧
  - 异步轮询竞争
  - 对系统时间、时区、时钟漂移敏感
- 真实行为回归
  - 输入不变但断言稳定失败
  - debug 和 release 都能复现

## Fast Checks

### 产物和命令

```powershell
ctest --test-dir build -N -V
Get-ChildItem build\runtime_release\bin
Get-ChildItem build\runtime_debug\bin
```

### 运行目录

```powershell
Get-ChildItem build\runtime_release\data_root
Get-ChildItem build\runtime_debug\data_root
```

### 进程和端口

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*stdiolink*' }
netstat -ano | Select-String ":<port>"
```

## Failure Smells

- 只在整包跑时失败：优先怀疑测试间污染、共享目录、残留进程
- 只在 release 失败：优先怀疑未初始化状态、竞态、优化相关 UB
- 只在 Windows 失败：优先怀疑路径分隔符、文件锁、端口释放延迟、PATH
- 错误信息里出现 `not found`、`required field`、`validation`：先看输入文件和运行目录
- 错误信息里出现 `timeout`、`wait`、`poll`：先看并发和环境负载

## Decision Rules

- 没确认测试程序路径前，不下代码回归结论
- 没确认运行目录前，不下资源缺失结论
- 单 case 可过但套件失败，先查污染，不要先改业务逻辑
- 同一失败能在隔离环境重复出现，再进入源码排查

## Related

- `verify-reported-failure.md`
- `test-artifact-and-runtime-layout.md`
- `../08-workflows/debug-change-entry.md`

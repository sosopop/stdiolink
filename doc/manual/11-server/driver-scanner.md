# Driver 扫描

`DriverManagerScanner` 负责扫描 `drivers/` 目录，发现 Driver 可执行文件并加载其元数据。相比核心库的 `DriverScanner`，它额外提供了 `.failed` 隔离策略、元数据自动导出和手动重扫能力。

## Driver 目录结构

每个 Driver 是 `drivers/` 下的一个子目录，包含可执行文件和元数据文件：

```
drivers/
├── driver_modbusrtu/
│   ├── stdio.drv.modbusrtu          # 可执行文件
│   └── driver.meta.json                    # 元数据（可自动导出）
├── driver_3dvision/
│   ├── stdio.drv.3dvision
│   └── driver.meta.json
└── driver_broken.failed/         # 导出失败，已隔离
    └── driver_broken
```

## 扫描流程

对 `drivers/` 下的每个子目录，按以下逻辑处理：

```
for each 子目录:
  if 目录名以 .failed 结尾:
    跳过 → skippedFailed++

  if 存在 driver.meta.json:
    if refreshMeta 模式:
      尝试重新导出覆盖
      导出失败 → 保留旧 meta（不标记 .failed）
    加载 meta
  else:
    尝试执行 Driver 导出 meta
    if 导出成功:
      加载 meta
    else:
      重命名目录为 <dir>.failed
      newlyFailed++
```

## 元数据自动导出

当 Driver 目录中不存在 `driver.meta.json` 时，Scanner 会自动执行 Driver 可执行文件进行导出：

```bash
<driver_executable> --export-meta=<driver_dir>/driver.meta.json
```

导出成功的判定条件：

- 进程在 10 秒内退出
- 退出码为 0
- 输出文件存在且为合法 JSON

导出失败时，该目录会被重命名为 `<dir>.failed`，后续扫描自动跳过。

## .failed 隔离与恢复

导出失败的 Driver 目录会被重命名加上 `.failed` 后缀，例如 `driver_broken` → `driver_broken.failed`。这是一种安全隔离机制，防止反复尝试启动有问题的 Driver。

恢复步骤：

1. 修复 Driver 可执行文件或配置
2. 手动去掉目录名中的 `.failed` 后缀
3. 通过 API 触发重扫：`POST /api/drivers/scan`

## refreshMeta 模式

默认启动时 `refreshMeta=true`，对已有 `driver.meta.json` 的目录也会尝试重新导出。这确保元数据与 Driver 可执行文件保持同步。

重新导出失败时的行为与首次导出不同：**保留旧 meta，不标记 .failed**，仅输出警告日志。

## 扫描统计

```
Drivers: 2 updated, 1 failed, 0 skipped
```

| 计数器 | 说明 |
|--------|------|
| `scanned` | 扫描的子目录数（不含 `.failed`） |
| `updated` | 成功加载/更新 meta 的 Driver 数 |
| `newlyFailed` | 本次新标记为 `.failed` 的目录数 |
| `skippedFailed` | 跳过的 `.failed` 目录数 |

## 通过 API 操作

```bash
# 列出已发现的 Driver
curl http://127.0.0.1:8080/api/drivers

# 手动触发重扫（含 meta 刷新）
curl -X POST http://127.0.0.1:8080/api/drivers/scan

# 仅扫描目录，跳过 meta 重新导出
curl -X POST http://127.0.0.1:8080/api/drivers/scan \
  -H "Content-Type: application/json" \
  -d '{"refreshMeta": false}'
```

手动重扫默认执行完整流程（含 refreshMeta），适用于新增 Driver 或恢复 `.failed` 目录后的场景。设置 `refreshMeta: false` 可跳过 meta 重新导出，仅扫描目录变更。

详见 [HTTP API 参考](http-api.md)。

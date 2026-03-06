# 里程碑 39：ServerManager 全链路 Demo 与发布脚本

> **前置条件**: 里程碑 34–38 已完成并通过现有单元测试
> **目标**: 在 `src/demo/` 提供可复现、可演示、可发布的 `stdiolink_server` 全功能示例，并补齐一键发布脚本

---

## 1. 目标

- 在 `src/demo/` 新增 `server_manager_demo`，覆盖 M34-M38 已实现能力
- 提供最小可运行的数据根目录模板（services/projects/drivers/workspaces/logs/shared）
- 提供演示入口脚本（初始化、启动、API 调用样例）
- 新增发布脚本，基于已编译产物生成结构化发布目录
- 保持对现有源码最小侵入，不改动核心运行逻辑，仅新增 demo/脚本/文档

---

## 2. 范围与非目标

### 2.1 范围（M39 内）

- `src/demo/server_manager_demo/` 的示例文件、示例 Service、示例 Project
- 面向 `stdiolink_server` 的演示说明文档
- `tools/publish_release.sh` 发布脚本（Unix-like）
- 必要的 CMake 资产复制逻辑（仅 demo 资源复制）

### 2.2 非目标（M39 外）

- 不新增 `stdiolink_server` 新接口，不改 API 协议
- 不引入鉴权、WebSocket 实时推送等新能力
- 不处理跨平台安装器（如 dmg/msi），仅提供目录式发布包

---

## 3. Demo 总体设计

### 3.1 目录规划

```
src/demo/server_manager_demo/
├── README.md
├── data_root/
│   ├── config.json
│   ├── services/
│   │   ├── quick_start_service/
│   │   │   ├── manifest.json
│   │   │   ├── config.schema.json
│   │   │   └── index.js
│   ├── projects/
│   │   ├── manual_demo.json
│   │   ├── fixed_rate_demo.json
│   │   └── daemon_demo.json
│   ├── drivers/
│   ├── workspaces/
│   ├── logs/
│   └── shared/
└── scripts/
    ├── run_demo.sh
    └── api_smoke.sh
```

说明：
- `drivers/` 初始可为空，用于演示 `/api/drivers/scan`
- `services/` 与 `projects/` 预置最小样例，用于演示扫描、校验、启动、停止、重载
- `logs/`、`workspaces/` 由 server 自动维护，demo 中仅保留目录占位

### 3.2 功能覆盖矩阵（对应 M34-M38）

| 里程碑 | 能力 | Demo 覆盖方式 |
|--------|------|---------------|
| M34 | Server 脚手架、Service 扫描 | 启动时加载 `services/` 并可通过 `/api/services` 查看 |
| M35 | Driver 扫描与 meta 刷新 | 调用 `/api/drivers/scan`，观察 `scanned/updated/newlyFailed/skippedFailed` |
| M36 | Project 管理 | 演示 `/api/projects` CRUD 与 `/validate` |
| M37 | Instance + Schedule | 演示 `manual/fixed_rate/daemon` 三类 project 的运行差异 |
| M38 | HTTP API 集成 | 提供一组可直接执行的 curl 命令覆盖主要接口 |

---

## 4. 实现步骤

### 4.1 Demo 资源准备

- 新建 `server_manager_demo` 目录与 README
- 创建 `data_root/config.json`，至少配置：
  - `host: "127.0.0.1"`
  - `port: 6200`（避免与默认 8080 冲突）
  - `logLevel: "info"`
  - `serviceProgram: "./stdiolink_service"`（由发布目录相对路径解析）
- 准备一个可稳定执行的 JS Service（快速返回 + 可观测日志）

### 4.2 预置 Project 样例

- `manual_demo.json`：`schedule.type = manual`
- `fixed_rate_demo.json`：`schedule.type = fixed_rate`，小周期触发
- `daemon_demo.json`：`schedule.type = daemon`，验证异常退出重启与抑制逻辑
- 所有 Project 配置遵循当前 `Project::fromJson` 与 `Schedule::fromJson` 约束

### 4.3 演示脚本

- `run_demo.sh`：
  - 启动 `stdiolink_server --data-root=<demo_data_root>`
  - 输出可访问 API 地址和建议操作
- `api_smoke.sh`：
  - 串行执行关键 API：services/projects/start/stop/instances/drivers
  - 对关键返回字段做最小断言（grep）

### 4.4 CMake 集成

- 在 `src/demo/CMakeLists.txt` 增加 `server_manager_demo` 资产复制目标
- 将 `src/demo/server_manager_demo/data_root` 复制到 `build/bin/server_manager_demo/data_root`
- 不新增新可执行文件，仅复制资源和脚本

### 4.5 发布脚本（先行交付）

- 新增 `tools/publish_release.sh`
- 输入：`build` 目录（已编译）
- 输出：`release/<package_name>/`
- 默认包含：
  - `bin/`：可执行文件（默认排除测试二进制，可选包含）
  - `demo/`：`config_demo`、`js_runtime_demo`、后续 `server_manager_demo`
  - `data_root/`：空模板目录结构
  - `doc/`：`doc/stdiolink_server.md` 与 `doc/milestone/milestone_39...`
  - `RELEASE_MANIFEST.txt`：版本/时间/commit/文件清单

---

## 5. 发布目录规范

```
release/<name>/
├── bin/
├── demo/
│   ├── config_demo/
│   ├── js_runtime_demo/
│   └── server_manager_demo/      # M39 完成后纳入
├── data_root/
│   ├── drivers/
│   ├── services/
│   ├── projects/
│   ├── workspaces/
│   ├── logs/
│   └── shared/
├── doc/
│   ├── stdiolink_server.md
│   └── milestone_39_server_manager_demo_and_release.md
└── RELEASE_MANIFEST.txt
```

---

## 6. 验收标准

- 可通过 `build/bin/stdiolink_server` + demo 数据目录完整启动
- 示例 API 可覆盖：
  - Service 列表/详情
  - Project 列表/创建/更新/删除/验证/启动/停止/重载
  - Instance 列表/终止/日志
  - Driver 列表/扫描
- 发布脚本可在本机重复执行，输出目录稳定、结构一致
- 发布目录在不依赖源码目录的情况下可独立运行基础演示

---

## 7. 风险与控制

- **风险 1**：发布包遗漏 demo 资产
  - 控制：脚本优先从 `build/bin` 复制，缺失时回退到 `src/demo` 复制
- **风险 2**：测试产物误入发布包
  - 控制：默认排除 `stdiolink_tests/test_*`，提供 `--with-tests` 开关
- **风险 3**：不同机器目录路径不一致
  - 控制：脚本支持 `--build-dir`、`--output-dir`、`--name` 参数化

---

## 8. 里程碑完成定义（DoD）

- 文档、demo 资源、脚本全部入库
- M39 新增脚本完成语法检查并通过最小手工验证
- 现有单元测试无回归
- 提供一份“从构建到启动演示到打包发布”的完整操作说明


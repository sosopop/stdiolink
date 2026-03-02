# 里程碑 91：Demo 演示资产 — `array<object>` Service 配置与 Project

> **前置条件**: 里程碑 90（`array<object>` 解析与渲染修复）已完成；`server_manager_demo` 基础目录结构已就绪
> **目标**: 在 `server_manager_demo` 中新增一个以 `array<object>` 为核心配置场景的演示 Service，配套可运行 Project，使 WebUI 项目配置页端到端演示 `array<object>` 增删改全流程

---

## 1. 目标

- 新增演示 Service `multi_device_service`：`config.schema.json` 以 `devices` 字段（`array<object>`）为主体，演示子字段类型覆盖（string/int/enum/bool）
- 提供配套 `index.js`：展示如何从配置中读取 `devices` 数组，并为每个设备 `openDriver`，体现 `array<object>` 的实际业务语义
- 新增两个演示 Project：`manual_multi_device`（手动触发）和 `daemon_multi_device`（daemon 调度），覆盖 Project 在不同调度模式下对 `array<object>` 配置的使用
- 更新 `server_manager_demo/scripts/api_smoke.sh` 覆盖新增路径（Service 详情、Project 创建、validate 接口）
- 不引入新 C++ 代码，不修改任何核心运行逻辑，仅新增演示资产

**子系统交付矩阵**：

| 子系统 | 交付项 |
|--------|--------|
| Demo 数据资产 | `multi_device_service/` 目录（manifest + schema + index.js），2 个 Project JSON |
| API 冒烟脚本 | `api_smoke.sh` 新增 `multi_device_service` 与对应 Project 的覆盖场景 |
| 文档 | `server_manager_demo/README.md` 更新，添加 `array<object>` 演示流程说明 |

---

## 2. 背景与问题

现有 `server_manager_demo` 已包含以下演示场景：

- `quick_start_service`：简单 string/int 字段，用于基础 Project 创建演示
- `driver_pipeline_service`：Driver 编排，配置为 string 类型
- `modbustcp_server_service` / `modbusrtu_server_service`：含复杂约束，但无 `array<object>`

M90 完成后，WebUI 的 `array<object>` 配置链路修复完毕，但缺少可直接启动演示的 Service 和 Project 资产。没有演示 Service，开发者无法端到端验收 WebUI 配置表单的渲染、子字段交互、配置保存全流程。

**范围**：
- `src/demo/server_manager_demo/data_root/services/multi_device_service/`（3 个文件）
- `src/demo/server_manager_demo/data_root/projects/manual_multi_device.json`
- `src/demo/server_manager_demo/data_root/projects/daemon_multi_device.json`
- `src/demo/server_manager_demo/scripts/api_smoke.sh`（追加脚本段）
- `src/demo/server_manager_demo/README.md`（追加说明段）

**非目标**：
- 不修改任何 C++ 源码
- 不新增 C++ 或 TypeScript 测试用例（验证通过静态校验 + 冒烟脚本）
- 不实现真实硬件驱动；`index.js` 中的 `openDriver` 调用使用 demo 目录中已有的 `stdio.drv.device-simulator` 或 `stdio.drv.calculator` 作为桩

---

## 3. 技术要点

### 3.1 目标 Schema 设计

`multi_device_service` 的 `config.schema.json` 以 `devices` 字段（`array<object>`）为主体，子字段故意覆盖多种类型，确保 WebUI ArrayField + ObjectField 各种子控件都被激活：

```json
{
  "service_name": {
    "type": "string",
    "required": true,
    "default": "multi-device-demo",
    "description": "服务实例名称"
  },
  "log_level": {
    "type": "enum",
    "required": false,
    "default": "info",
    "description": "日志级别",
    "constraints": {
      "enumValues": ["debug", "info", "warn", "error"]
    }
  },
  "devices": {
    "type": "array",
    "required": true,
    "description": "设备列表，每项为一台设备的连接配置",
    "constraints": {
      "minItems": 1,
      "maxItems": 8
    },
    "items": {
      "type": "object",
      "description": "单台设备配置",
      "fields": {
        "id": {
          "type": "string",
          "required": true,
          "description": "设备唯一标识，格式：device-xxx"
        },
        "host": {
          "type": "string",
          "required": true,
          "default": "127.0.0.1",
          "description": "设备 IP 地址或主机名"
        },
        "port": {
          "type": "int",
          "required": true,
          "default": 2368,
          "description": "设备通信端口",
          "constraints": {
            "min": 1,
            "max": 65535
          }
        },
        "driver": {
          "type": "enum",
          "required": true,
          "default": "simulator",
          "description": "使用的 Driver 类型",
          "constraints": {
            "enumValues": ["simulator", "modbus", "canbus"]
          }
        },
        "enabled": {
          "type": "bool",
          "required": false,
          "default": true,
          "description": "是否启用此设备"
        },
        "timeout_ms": {
          "type": "int",
          "required": false,
          "default": 5000,
          "description": "连接超时（毫秒）",
          "constraints": {
            "min": 100,
            "max": 30000
          }
        }
      }
    }
  }
}
```

**字段类型覆盖矩阵**（确保每类子控件都被测试）：

| 字段 | 类型 | 目的 |
|------|------|------|
| `id` | string | StringField |
| `host` | string | StringField + 默认值 |
| `port` | int | NumberField + min/max 约束 |
| `driver` | enum | EnumField + enumValues |
| `enabled` | bool | BoolField |
| `timeout_ms` | int | NumberField + 可选字段 |

### 3.2 演示 `index.js` 设计

> **前置假设**：`index.js` 使用 ES Module 裸模块导入（`import { getConfig } from 'stdiolink'`）和 ES6+ 语法（`async/await`、`for...of`、`filter` 等）。这些能力依赖 M12-M33 已完成的 QuickJS 宿主集成，具体为 `engine/module_loader.cpp` 中通过 `JS_SetModuleLoaderFunc` 注册的自定义模块加载器，以及 `bindings/` 下各 JS 绑定模块（`stdiolink`、`stdiolink/log`、`stdiolink/driver` 等）。现有 demo service（如 `driver_pipeline_service/index.js`）已验证该机制可正常工作。若后续修改模块加载器或绑定接口，需同步确认本 demo 脚本仍可正常加载。

`index.js` 展示 `array<object>` 配置的业务使用场景：读取 `devices` 数组，过滤 `enabled=true` 的设备，对每台设备 `openDriver` 并执行一次探活命令：

```js
import { getConfig, openDriver } from 'stdiolink';
import { createLogger } from 'stdiolink/log';
import { resolveDriver } from 'stdiolink/driver';

const config = getConfig();
const { service_name, log_level, devices } = config;

const logger = createLogger({ service: service_name });

logger.info('multi_device_service starting', {
  logLevel: log_level,
  totalDevices: devices.length,
});

const enabledDevices = devices.filter(d => d.enabled !== false);
logger.info(`enabling ${enabledDevices.length} / ${devices.length} devices`);

(async () => {
  for (const device of enabledDevices) {
    logger.info(`connecting device`, { id: device.id, host: device.host, port: device.port });

    // 使用 resolveDriver 解析驱动路径（demo 中统一映射到 device-simulator）
    const driverPath = resolveDriver('stdio.drv.device-simulator');

    try {
      const driver = await openDriver(driverPath);

      // 通过 Proxy 语法发送探活命令
      const result = await driver.scan({ mode: 'quick' });
      logger.info(`device ${device.id} responded`, { result });

      driver.$close();
    } catch (err) {
      logger.warn(`device ${device.id} connection failed`, { error: String(err) });
    }
  }

  logger.info('multi_device_service completed');
})();
```

### 3.3 Project JSON 结构

**`manual_multi_device.json`**（手动触发，用于 WebUI 配置页演示）：

```json
{
  "name": "多设备演示（手动）",
  "serviceId": "multi_device_service",
  "enabled": true,
  "schedule": {
    "type": "manual"
  },
  "config": {
    "service_name": "demo-manual",
    "log_level": "info",
    "devices": [
      {
        "id": "device-001",
        "host": "192.168.1.10",
        "port": 2368,
        "driver": "simulator",
        "enabled": true,
        "timeout_ms": 5000
      },
      {
        "id": "device-002",
        "host": "192.168.1.11",
        "port": 2369,
        "driver": "simulator",
        "enabled": true,
        "timeout_ms": 3000
      }
    ]
  }
}
```

**`daemon_multi_device.json`**（daemon 调度，展示 `array<object>` 与调度机制联动）：

```json
{
  "name": "多设备演示（守护进程）",
  "serviceId": "multi_device_service",
  "enabled": false,
  "schedule": {
    "type": "daemon",
    "restartDelay": 3000
  },
  "config": {
    "service_name": "demo-daemon",
    "log_level": "debug",
    "devices": [
      {
        "id": "device-alpha",
        "host": "127.0.0.1",
        "port": 9001,
        "driver": "simulator",
        "enabled": true,
        "timeout_ms": 2000
      }
    ]
  }
}
```

> `enabled: false` 避免 demo 启动时 daemon 自动运行失败，用户可在 WebUI 手动启用。

### 3.4 API 冒烟脚本新增段

追加到 `scripts/api_smoke.sh`，覆盖 `multi_device_service` 相关的 API 路径：

> **Windows 运行说明**：`api_smoke.sh` 为 Bash 脚本，Windows 环境下需通过 Git Bash 或 WSL 执行，与项目现有 `tools/run_tests.sh` 等脚本的运行方式一致。

```bash
# ── multi_device_service 演示场景 ─────────────────────────────────────────

show_step "[M91] GET /api/services/multi_device_service"
resp="$(call_json GET /api/services/multi_device_service)"
echo "${resp}" | grep -q '"devices"'

show_step "[M91] POST /api/projects/manual_multi_device/validate - valid"
call_json POST /api/projects/manual_multi_device/validate \
  '{"config":{"service_name":"smoke-test","log_level":"info","devices":[{"id":"d1","host":"127.0.0.1","port":2368,"driver":"simulator","enabled":true}]}}'

show_step "[M91] POST /api/projects/manual_multi_device/validate - unknown subfield"
call_json POST /api/projects/manual_multi_device/validate \
  '{"config":{"service_name":"smoke-test","log_level":"info","devices":[{"id":"d1","host":"127.0.0.1","port":2368,"driver":"simulator","enabled":true,"bad_key":"x"}]}}'
```

---

## 4. 实现步骤

### 4.1 新增 `multi_device_service` 目录与三文件

创建目录：`src/demo/server_manager_demo/data_root/services/multi_device_service/`

**4.1.1 `manifest.json`**：

```json
{
  "manifestVersion": "1",
  "id": "multi_device_service",
  "name": "多设备接入服务",
  "version": "1.0.0",
  "description": "演示 array<object> 配置：支持配置多台设备，每台设备含 host/port/driver/enabled 等子字段",
  "author": "stdiolink-demo"
}
```

**4.1.2 `config.schema.json`**：写入 §3.1 中的完整 JSON。

**4.1.3 `index.js`**：写入 §3.2 中的完整 JS。

**改动理由**：三文件均为纯静态 JSON/JS，通过 `ServiceScanner` 扫描后自动出现在 `/api/services`，无需修改任何 C++ 代码。

**验收方式**：`GET /api/services/multi_device_service` 返回 200，`configSchemaFields` 中 `devices` 字段含完整 `items.fields`（6 个子字段）。

---

### 4.2 新增 Project JSON 文件

**`manual_multi_device.json`**：写入 §3.3 中的完整 JSON，存放于 `data_root/projects/`。

**`daemon_multi_device.json`**：写入 §3.3 中的完整 JSON，存放于 `data_root/projects/`。

**改动理由**：Project 文件通过 `ProjectManager` 扫描加载，`enabled: false` 的 daemon project 不自动启动，安全用于演示。

**验收方式**：`GET /api/projects` 返回列表中含两个新增 Project，`manual` 模式 Project 可通过 `POST /api/projects/:id/start` 手动触发。

---

### 4.3 更新冒烟脚本与 README

**`scripts/api_smoke.sh`**：在文件末尾追加 §3.4 中的脚本段。

**`README.md`**：在"功能覆盖矩阵"章节后追加：

```markdown
### array<object> 配置演示（M91）

Service `multi_device_service` 专门用于演示 WebUI 对 `array<object>` 类型配置的完整支持：

1. 在 WebUI 的 Services 页面找到"多设备接入服务"
2. 查看 Schema 编辑器：`devices` 字段类型为 `array`，每项含 6 个子字段
3. 在 Projects 页面找到"多设备演示（手动）"，点击"编辑配置"
4. 在配置表单中点击"添加设备"，填写 host / port / driver / enabled，保存
5. 点击 Validate：验证通过则配置合法
6. 在任意 device 中填入未知字段并 Validate：观察错误路径格式 `devices[N].bad_key`
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/demo/server_manager_demo/data_root/services/multi_device_service/manifest.json` — Service 身份声明
- `src/demo/server_manager_demo/data_root/services/multi_device_service/config.schema.json` — 含 `array<object>` 的配置字段定义（6 子字段）
- `src/demo/server_manager_demo/data_root/services/multi_device_service/index.js` — 读取 devices 数组并为每设备 openDriver 的编排脚本

- `src/demo/server_manager_demo/data_root/projects/manual_multi_device.json` — 手动触发 Project，含 2 台设备配置
- `src/demo/server_manager_demo/data_root/projects/daemon_multi_device.json` — daemon 调度 Project，含 1 台设备配置（默认禁用）

### 5.2 修改文件

- `src/demo/server_manager_demo/scripts/api_smoke.sh` — 追加 `multi_device_service` 相关冒烟场景
- `src/demo/server_manager_demo/README.md` — 追加 array<object> 演示流程说明

### 5.3 测试文件

无新增测试文件（验证通过静态格式校验 + api_smoke.sh 集成冒烟完成，参见 §6.1）

---

## 6. 测试与验收

### 6.1 单元测试

- **测试对象**：静态文件格式正确性（manifest.json / config.schema.json / index.js / project JSON）
- **用例分层**：格式正确性、必填字段完整性、引用一致性、业务语义合规
- **断言要点**：JSON.parse 不抛异常；manifest id 与目录名一致；schema 字段 type/description 完整；Project serviceId 引用存在
- **桩替身**：无需 mock（纯静态文件校验）
- **测试文件**：通过 shell 脚本或 Node.js 一次性脚本完成（不进入 C++/TS 测试套件）

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| manifest.json 格式 | 合法 JSON 且含 manifestVersion/id/name/version/description | T01 |
| manifest.json id 一致性 | id 字段值 == 目录名 `multi_device_service` | T02 |
| config.schema.json 格式 | 合法 JSON | T03 |
| `devices` 字段类型声明 | `type == "array"`，含 `constraints.minItems/maxItems`，含 `items.fields` | T04 |
| `devices.items.fields` 完整性 | 含 id/host/port/driver/enabled/timeout_ms 共 6 个字段 | T05 |
| `devices.items.fields` 类型覆盖 | string/int/enum/bool 四种类型均出现 | T06 |
| `devices.items.fields.port` 约束 | `constraints.min == 1`，`constraints.max == 65535` | T07 |
| `devices.items.fields.driver` enum | `constraints.enumValues` 含 `["simulator","modbus","canbus"]` | T08 |
| index.js 语法 | 可被 QuickJS 引擎解析，无语法错误 | T09 |
| Project JSON 格式 | 两个 Project 均为合法 JSON | T10 |
| Project serviceId 引用 | `serviceId == "multi_device_service"` | T11 |
| Project config.devices | devices 数组非空，每项含 id/host/port/driver | T12 |
| daemon Project 默认禁用 | `daemon_multi_device.json` 中 `enabled == false` | T13 |

- 覆盖要求：所有路径均可达，路径矩阵仅作为手工检查清单，不编写独立断言脚本

#### 测试代码（轻量 JSON 格式校验）

静态文件的格式正确性通过 CI 中的轻量校验完成，不单独编写详细断言脚本：

```bash
# CI 中一行校验 JSON 格式合法性即可
jq . src/demo/server_manager_demo/data_root/services/multi_device_service/manifest.json > /dev/null
jq . src/demo/server_manager_demo/data_root/services/multi_device_service/config.schema.json > /dev/null
jq . src/demo/server_manager_demo/data_root/projects/manual_multi_device.json > /dev/null
jq . src/demo/server_manager_demo/data_root/projects/daemon_multi_device.json > /dev/null
```

> **设计决策**：对手工创建的静态 JSON 文件逐字段做断言（如 `id == "multi_device_service"`、`fields 数量 == 6`）本质上是在重复验证自己刚写的常量，无法发现真正的缺陷。真正有价值的验证是通过 `api_smoke.sh` 做集成冒烟（确认 Server 能正确加载和解析这些文件），以及 WebUI 手工验收。因此不再提供独立的断言脚本；上述路径矩阵仅作为代码审查时的手工检查清单。
>
> **注意**：T09（`index.js` 语法）通过 `api_smoke.sh` 中 `GET /api/services/multi_device_service` 返回 200 间接验证（QuickJS 引擎加载时会做语法检查）。`index.js` 使用 ES Module `import` 语法，与项目现有 Service 一致，由 QuickJS 原生支持，不可用 `node --check` 校验。

### 6.2 集成 / API 冒烟测试

通过更新后的 `api_smoke.sh` 覆盖以下路径：

- `GET /api/services/multi_device_service`：返回 200，`configSchemaFields` 中 `devices` 含 `items.fields`（6 项）
- `GET /api/projects`：新增两个 Project 出现在列表中
- `POST /api/projects/:id/validate`（合法配置）：`{"valid": true}`
- `POST /api/projects/:id/validate`（含未知子字段）：`{"valid": false, "errorField": "devices[0].bad_key"}`

### 6.3 验收标准

- [ ] `GET /api/services/multi_device_service` 返回 200，`configSchemaFields` 中 `devices.items.fields` 长度为 6（对应 T05）
- [ ] WebUI 项目配置页打开 `manual_multi_device` Project，`devices` 字段渲染为数组列表，点击"添加设备"后弹出含 6 个子字段的子表单（依赖 M90 修复）
- [ ] 填写有效子字段后保存，`POST /api/projects/:id/validate` 返回 `{"valid": true}`
- [ ] 在任意 device 项添加未知字段后 Validate，错误路径格式为 `"devices[N].bad_key"`（对应 M90 修复验收）
- [ ] `api_smoke.sh` 新增段全部通过，无非零退出码
- [ ] 静态 JSON 文件 `jq` 格式校验通过
- [ ] 现有测试套件无回归

**回归执行入口**：

```powershell
# 全量回归（确认新增静态文件未影响现有功能）
tools/run_tests.ps1

# 仅前端单元测试（确认 SchemaForm 渲染无回归）
tools/run_tests.ps1 --vitest
```

---

## 7. 风险与控制

- 风险：`index.js` 中引用的 `stdio.drv.device-simulator` 在 demo 环境不存在，导致服务启动失败
  - 控制：demo Project 中 `manual_multi_device` 为 manual 模式，不自动启动；`daemon_multi_device` 默认 `enabled: false`；README 中明确说明"demo 仅演示配置链路，不依赖真实 Driver 存在"
  - 控制：`index.js` 顶层添加注释 `// demo script: requires stdio.drv.device-simulator in data_root/drivers/`，指引开发者按需准备

- 风险：`config.schema.json` 中 `items.fields` 格式写错，导致 M90 修复后的解析仍无法正确处理
  - 控制：`api_smoke.sh` 在集成阶段通过 API 响应校验 `items.fields` 存在；静态阶段通过 `jq` 校验 JSON 格式合法性
  - 测试覆盖：T04 断言 `items.fields` 为 object 类型，T05 断言子字段数量

- 风险：Project JSON 中 `config.devices` 字段值与 schema 不一致（如字段拼写错误），Validate 接口报错
  - 控制：T12 校验每个 device 项含全部必填字段；api_smoke.sh 执行 validate 并断言 `valid == true`
  - 控制：daemon Project 默认禁用，不会在 demo 启动时触发自动校验失败

---

## 8. 里程碑完成定义（DoD）

- [ ] `multi_device_service` 三文件创建完毕（manifest / schema / index.js）
- [ ] 两个 Project JSON 文件创建完毕（manual / daemon）
- [ ] 静态 JSON 文件格式校验通过（`jq` 解析无报错）
- [ ] `api_smoke.sh` 新增段执行无报错（需先启动 demo server）
- [ ] WebUI 手工验收：`multi_device_service` 的 Project 配置页可完整操作 `devices` 数组（新增/删除/编辑子字段），保存后 Validate 通过
- [ ] `README.md` 更新完成，含 `array<object>` 演示步骤说明
- [ ] 本里程碑文档入库 `doc/milestone/`

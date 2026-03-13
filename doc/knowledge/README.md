# stdiolink AI Knowledge Base

面向 AI 开发检索的高密度知识库。目标是用最少 token 快速定位概念、实现入口、修改链路和相关测试。

## Use This Index

- 新需求先读 `00-overview/README.md`，确认分层、运行时布局和术语。
- 改协议或元数据读 `01-protocol/README.md`。
- 开发 C++ Driver 读 `02-driver/README.md`。
- 改 Host 端进程管理、任务并发、表单生成读 `03-host/README.md`。
- 开发 JS Service、绑定、配置读 `04-service/README.md`。
- 改 Server 编排、扫描、HTTP/SSE/WS 读 `05-server/README.md`。
- 改前端管理台或 DriverLab 读 `06-webui/README.md`。
- 跑构建、测试、发布读 `07-testing-build/README.md`。
- 需要完整实现路径时，从 `08-workflows/README.md` 进入。

## Directory Map

- `00-overview/`：系统地图、术语、运行时目录、源码总入口。
- `01-protocol/`：JSONL 消息模型、元数据类型、验证链。
- `02-driver/`：DriverCore 生命周期、命令处理、响应、元数据导出。
- `03-host/`：Host Driver/Task/waitAny/form 生成。
- `04-service/`：QuickJS 运行时、内置绑定、Service 配置与 Driver 编排。
- `05-server/`：Service/Project/Instance 生命周期、调度、HTTP 与实时通信。
- `06-webui/`：React 前端结构、页面职责、DriverLab 与事件流。
- `07-testing-build/`：构建布局、测试矩阵、发布与本地调试。
- `08-workflows/`：跨子系统开发路径。

## Query Routing

- “新增一个 Driver” -> `08-workflows/add-driver.md` -> `02-driver/driver-lifecycle.md` -> `02-driver/driver-meta.md`
- “新增/修改 Service” -> `08-workflows/add-service-or-project.md` -> `04-service/service-runtime.md` -> `04-service/service-config-and-driver.md`
- “修改协议/参数校验” -> `01-protocol/message-model.md` -> `01-protocol/meta-validation.md`
- “加 Server API/调度能力” -> `05-server/server-lifecycle.md` -> `05-server/server-api-realtime.md`
- “前端接新接口/改实时联调” -> `06-webui/webui-structure.md` -> `06-webui/driverlab-and-events.md`
- “用户贴了一段测试失败日志，先确认问题是否仍存在” -> `07-testing-build/verify-reported-failure.md`
- “不知道先跑哪层测试最合适” -> `07-testing-build/choose-test-entry.md`
- “测试失败像是环境/产物/运行目录问题” -> `07-testing-build/triage-test-failure.md` -> `07-testing-build/test-artifact-and-runtime-layout.md`
- “排查改动落点” -> `08-workflows/debug-change-entry.md`
- “本次改动要更新哪些知识库” -> `08-workflows/update-knowledge-base.md`
- “根据这次问答/排障过程反思并优化知识库路径” -> `08-workflows/update-knowledge-base.md`

## Source of Truth

- 详细手册：`doc/manual/`
- 开发总览：`doc/developer-guide.md`
- 核心库：`src/stdiolink/`
- JS 运行时：`src/stdiolink_service/`
- 管控后端：`src/stdiolink_server/`
- 前端：`src/webui/`
- 示例与模板：`src/drivers/`、`src/data_root/services/`、`src/data_root/projects/`
- 测试：`src/tests/`、`src/smoke_tests/`

## Maintenance Rules

- 每个目录必须有 `README.md`，只做索引，不承载长正文。
- 目录索引 `README.md` 不需要 `Read Order`；保留 Files / Source Anchors 这类稳定索引即可。
- 根 `README.md` 尽量只保留通用目录入口和通用问题路由，不收录具体业务问题。
- 具体问题的索引优先下沉到最下级 `README.md` 或对应主题文档。
- 单篇文档只覆盖一个主题或一个链路。
- 正文优先写：修改入口、约束、依赖、失败点、测试入口。
- 知识库目标是帮助理解项目结构、主要流程和代码入口；正文不贴具体代码，实现细节以下游源码为准。
- 避免复制 `doc/manual/` 的长段说明；这里只保留决策信息和跳转。
- 任何公共行为、协议、元数据、配置结构、API、调试路径、测试路径变更后，都必须同步检查是否需要更新知识库。
- 先更新受影响主题文档，再更新对应目录 `README.md`，最后检查根索引是否需要补新的检索入口。
- 如果新增了新的开发路径、联动规则或高频排障经验，应优先补到 `08-workflows/` 而不是塞进记忆文件。

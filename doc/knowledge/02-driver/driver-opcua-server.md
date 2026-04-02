# OPC UA Server Driver

## Purpose

说明 `stdio.drv.opcua_server` 的命令面、节点模型、Service/Project 接入点与测试入口。

## Commands

- `status`：查询驱动与服务端状态
- `run`：启动服务端、装载节点并进入 keepalive 事件流；成功后仅发送 `started` 事件，不返回终态 `done`
- `start_server`：启动 OPC UA Server 监听
- `stop_server`：停止 OPC UA Server
- `upsert_nodes`：批量创建或更新 `folder` / `variable`
- `delete_nodes`：批量删除节点，目录节点需显式 `recursive=true`
- `write_values`：批量更新可写变量节点值
- `inspect_node`：按 `node_id` 查询单节点
- `snapshot_nodes`：导出业务树快照

## Implementation Entry

- `src/drivers/driver_opcua_server/handler.*`
- `src/drivers/driver_opcua_server/opcua_server_runtime.*`
- `src/drivers/opcua_common.*`
- `src/data_root/services/opcua_server_service/`
- `src/data_root/projects/opcua_server/`

## Node Model

- v1 只支持 `folder`、`variable`
- 业务节点统一使用 `NodeId` 主定位，不引入 `path`
- 自定义业务节点固定使用单一自定义 namespace，约定为 `ns=1`
- 顶层业务根默认挂在 `ObjectsFolder(i=85)` 下
- `int64` / `uint64` 在 JSON 输入输出里统一走十进制字符串
- `bytestring` 用 base64，`datetime` 用 ISO8601

## Event Model

- `started`
- `stopped`
- `session_activated`
- `session_closed`
- `node_value_changed`

`node_value_changed` 统一覆盖两种写入来源：

- `command_write`：通过 `write_values` 命令写入
- `external_write`：外部 OPC UA 客户端写入 `read_write` 节点

## Constraints

- v1 只支持匿名访问与 `SecurityPolicy#None`
- 服务端驱动必须是 `keepalive`，不要和客户端无状态驱动合并
- `run` 的生命周期语义需要和 `modbustcp_server` 保持一致：成功后常驻，不要回 `done` 导致 oneshot 自动退出
- `upsert_nodes` 允许父子节点无序输入，但实现必须做多轮解析；剩余孤儿节点要整体报错
- `delete_nodes` 对目录节点默认拒绝删除，除非显式 `recursive=true`
- `snapshot_nodes` 默认根为 `i=85`，但不把 `0:Server`、`0:Types` 等系统树混进业务快照
- open62541 的底层日志必须静默，不能污染 stdout JSONL 协议

## Implementation Pitfalls

- 不要把服务端能力继续塞进 `stdio.drv.opcua`。客户端是 `oneshot` / 无状态模型，服务端是 `keepalive` / 长生命周期模型，混在一个 handler 里后续很难维护。
- open62541 的写回调和命令写入会走同一条值更新链路；如果不区分来源，`write_values` 很容易重复发两次 `node_value_changed`。实现里要显式抑制命令写入时的 onWrite 回调事件。
- `browse_name` 是 `QualifiedName` 的 name 部分，不要把 `ns=1:` 这样的前缀再次塞进 `browse_name`，否则节点树看起来会正常但浏览行为会变怪。
- `UA_Server_addNamespace()` 返回的 index 不是“想当然永远等于 1”；v1 可以约束业务 namespace 固定映射到 `ns=1`，但实现里必须在启动时做显式校验，避免后续节点定义和真实 namespace 漂移。
- 外部客户端写值成功不代表缓存一定同步；如果节点上下文没绑好，事件里可能拿不到旧值、显示名和来源。变量节点创建/更新后要重新安装 `nodeContext` 与 `valueCallback`。
- 用 Service 启动 keepalive 驱动时，JS 层不要自己做无限重试。失败就直接退出，让 daemon Project 按统一调度策略拉起，避免双层重试把问题掩盖掉。

## Test Entry

- `src/tests/test_opcua_server_driver.cpp`
- `src/tests/test_opcua_server_service.cpp`
- `src/smoke_tests/m107_opcua_server.py`
- `build\\runtime_release\\bin\\stdiolink_tests.exe --gtest_filter=*OpcUaServer*`

## Related

- `driver-opcua.md`
- `driver-lifecycle.md`
- `../04-service/service-config-and-driver.md`
- `../08-workflows/add-driver.md`
- `../08-workflows/add-service-or-project.md`

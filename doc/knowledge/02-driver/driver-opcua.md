# OPC UA Driver

## Purpose

说明 `stdio.drv.opcua` 的命令面、节点快照规则、实现入口与测试入口。

## Commands

- `status`：固定返回 `ready`
- `inspect_node`：按 `host + port + node_id` 查询单节点；目录节点支持 `recurse`
- `snapshot_nodes`：从 `ObjectsFolder` 递归抓完整业务树快照，默认过滤标准系统节点

## Implementation Entry

- `src/drivers/driver_opcua/handler.*`
- `src/drivers/driver_opcua/main.cpp`
- 根构建需先解析 `open62541`，测试同样复用该包

## Constraints

- 节点输入只支持原生 `NodeId` 字符串，不支持 BrowsePath
- 连接是无状态的；每条命令独立建连、读取、断开
- 驱动必须屏蔽 open62541 底层 stdout 日志，保证对外只输出 JSONL 协议消息
- 目录节点只沿前向 `HierarchicalReferences` 遍历
- `snapshot_nodes` 以 `ObjectsFolder` 为根，但默认不把 `Server/Types/ReferenceTypes` 这类标准系统树带进结果
- 递归遍历必须对规范化 `NodeId` 做 visited 去重，避免环引用

## Test Entry

- `src/tests/test_opcua_driver.cpp`
- `build\\runtime_release\\bin\\stdiolink_tests.exe --gtest_filter=*OpcUa*`

## Related

- `driver-lifecycle.md`
- `driver-meta.md`
- `../08-workflows/add-driver.md`

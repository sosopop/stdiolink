# OPC UA Driver

## Purpose

说明 `stdio.drv.opcua` 的命令面、节点快照规则、实现入口与测试入口。

服务端能力已拆分到独立的 `stdio.drv.opcua_server`，不要继续在客户端驱动里叠加长生命周期的 Server 逻辑。

## Commands

- `status`：固定返回 `ready`
- `inspect_node`：按 `host + port + node_id` 查询单节点；目录节点支持 `recurse`
- `snapshot_nodes`：从 `ObjectsFolder` 递归抓完整业务树快照，默认过滤标准系统节点

## Implementation Entry

- `src/drivers/driver_opcua/handler.*`
- `src/drivers/driver_opcua/main.cpp`
- `src/drivers/opcua_common.*`
- 根构建需先解析 `open62541`，测试同样复用该包

## Constraints

- 节点输入只支持原生 `NodeId` 字符串，不支持 BrowsePath
- 连接是无状态的；每条命令独立建连、读取、断开
- 驱动必须屏蔽 open62541 底层 stdout 日志，保证对外只输出 JSONL 协议消息
- 目录节点只沿前向 `HierarchicalReferences` 遍历
- `snapshot_nodes` 以 `ObjectsFolder` 为根，但默认不把 `Server/Types/ReferenceTypes` 这类标准系统树带进结果
- 递归遍历必须对规范化 `NodeId` 做 visited 去重，避免环引用

## Implementation Pitfalls

- 不能直接用 `UA_Client_new()` 做静默驱动；它会先带上默认 stdout logger 和 event loop。需要先给 `UA_ClientConfig.logging` 注入静默 logger，再 `UA_ClientConfig_setDefault`，最后走 `UA_Client_newWithConfig()`。
- `snapshot_nodes` 从 `ObjectsFolder` 起扫时，默认会带出 `0:Server` 等标准系统树；如果命令语义是“业务树快照”，根层需要显式过滤 namespace 0 的系统节点。
- `recurse=false` 不等于“不返回 children”；实现上要区分“展开直属子节点”和“继续递归下钻”两个开关，否则很容易把一层浏览和递归浏览写混。
- 如果要验证驱动 stdout 只有 JSONL，优先起独立驱动进程做断言；在同进程里跑 open62541 测试服务器时，服务端自己的日志也可能污染捕获结果。
- `stdiolink_tests.exe` 不能直接在 `build\\release\\` 下运行，必须放在 `build\\runtime_*\\bin\\` 且旁边有 `data_root\\` 运行时布局，否则测试框架会先因为运行环境不完整而失败。
- Windows 下如果构建卡在 vcpkg 的 `applocal.ps1`，优先检查缓存里的 `Z_VCPKG_PWSH_PATH` 是否还指向已升级或已失效的 PowerShell 路径。

## Test Entry

- `src/tests/test_opcua_driver.cpp`
- `build\\runtime_release\\bin\\stdiolink_tests.exe --gtest_filter=*OpcUa*`

## Related

- `driver-lifecycle.md`
- `driver-opcua-server.md`
- `driver-meta.md`
- `../08-workflows/add-driver.md`

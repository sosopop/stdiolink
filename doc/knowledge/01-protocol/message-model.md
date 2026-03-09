# Message Model

## Purpose

理解 Host 和 Driver 之间传输什么、何时结束、哪些状态可流式出现。

## Request

- 结构：`{"cmd":"<name>","data":<json>}`
- `cmd` 是命令名；`data` 是参数对象或其他 JSON 值。
- 内置命令至少包括 `meta.describe`；文档中还存在 `meta.validate` 语义。

## Response

- 结构：`{"status":"event|done|error","code":<int>,"data":<json>}`
- `event` 可出现多次，不终止请求。
- `done` / `error` 二选一，出现后请求结束。

## Implementation Entry

- 解析：`src/stdiolink/protocol/jsonl_parser.*`
- 序列化：`src/stdiolink/protocol/jsonl_serializer.*`
- 流式按行解析：`src/stdiolink/protocol/jsonl_stream_parser.*`
- 类型定义：`src/stdiolink/protocol/jsonl_types.h`

## Constraints

- 一行一条 JSON；不要跨行。
- UTF-8；适合文本管道调试。
- Windows 管道读取要沿用 Qt 行读取链路，不要切到 `fread`/原始阻塞读。

## Modify Entry

- 改消息字段或状态语义时，同时检查 Host `Driver`、Driver `DriverCore`、JS Task/Proxy 绑定和相关测试。

## Related

- `meta-validation.md`
- `../02-driver/driver-lifecycle.md`
- `../03-host/driver-task-flow.md`

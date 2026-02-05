# 术语表

## A

**AnyItem**
`waitAnyNext` 返回的结果结构，包含任务索引和消息。

## C

**CommandMeta**
命令元数据，描述一个命令的名称、参数、返回值等。

**Console 模式**
Driver 的命令行交互模式，用于调试和独立使用。

**Constraints**
字段约束条件，如数值范围、字符串长度等。

## D

**Driver**
工作进程，接收 Host 的请求并执行任务。

**DriverCore**
Driver 端的核心类，处理通信和命令分发。

**DriverMeta**
驱动元数据，描述 Driver 的完整能力。

## F

**FieldMeta**
字段元数据，描述单个参数的类型和约束。

**FieldType**
字段类型枚举，如 String、Int、Bool 等。

## H

**Host**
主控进程，负责启动和管理 Driver。

## I

**ICommandHandler**
命令处理器接口。

**IMetaCommandHandler**
支持元数据的命令处理器接口。

**IResponder**
响应输出接口。

## J

**JSONL**
JSON Lines 格式，每行一个 JSON 对象。

## M

**Message**
响应消息结构。

**MetaValidator**
参数验证器。

## S

**Stdio 模式**
标准 IPC 模式，通过 stdin/stdout 通信。

## T

**Task**
请求句柄，用于获取响应。

## U

**UIHint**
UI 渲染提示。

**UiGenerator**
UI 表单生成器。

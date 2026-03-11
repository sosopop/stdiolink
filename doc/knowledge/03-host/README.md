# Host

本目录覆盖 Host 端如何启动 Driver、管理请求 Task、做并发等待和消费元数据。

## Files

- `driver-task-flow.md`：`Driver`/`Task` 基本链路、早退处理、关键入口。
- `wait-any-and-forms.md`：并发等待与表单生成的实现落点和风险。

## Source Anchors

- `src/stdiolink/host/`
- `doc/manual/06-host/`
- `src/tests/test_host_driver.cpp`
- `src/tests/test_wait_any.cpp`

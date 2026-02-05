# Host 端概述

Host 是 stdiolink 中的主控进程，负责启动和管理 Driver 进程。

## 核心组件

| 组件 | 文件 | 说明 |
|------|------|------|
| Driver | `driver.h` | Driver 进程管理 |
| Task | `task.h` | 请求句柄 |
| waitAnyNext | `wait_any.h` | 多任务等待 |
| UiGenerator | `form_generator.h` | UI 表单生成 |

## 开发流程

1. 创建 `Driver` 实例
2. 调用 `start()` 启动进程
3. 调用 `request()` 发送请求
4. 通过 `Task` 获取响应
5. 调用 `terminate()` 关闭进程

## 详细文档

- [Driver 类](driver-class.md)
- [Task 句柄](task.md)
- [多任务并行等待](wait-any.md)
- [UI 表单生成器](form-generator.md)

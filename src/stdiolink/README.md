# stdiolink 基础库

stdiolink IPC 框架的核心实现。

## 模块说明

| 目录 | 说明 |
|------|------|
| protocol/ | JSONL 协议解析与序列化 |
| driver/ | Driver 端（被调用方）实现 |
| host/ | Host 端（调用方）实现 |
| console/ | Console 模式参数解析 |

## 依赖

- Qt5 Core

## 使用方式

在 CMakeLists.txt 中链接 stdiolink 库即可使用。

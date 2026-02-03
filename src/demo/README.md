# 示例程序

展示如何使用 stdiolink 框架的示例程序。

## 目录说明

| 目录 | 说明 |
|------|------|
| host_demo/ | Host 端示例，演示如何调用 Driver |
| echo_driver/ | Echo Driver，简单回显命令 |
| progress_driver/ | Progress Driver，演示事件流 |

## 运行示例

```bash
# 先构建
ninja -C build_ninja

# 运行 host_demo
./build_ninja/bin/host_demo.exe
```

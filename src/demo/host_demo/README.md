# Host Demo

演示如何使用 stdiolink Host 端 API 调用 Driver 进程。

## 功能

1. 启动 echo_driver 和 progress_driver
2. 发送请求并接收响应
3. 演示 waitAnyNext 多 Driver 并发

## 构建

```bash
ninja -C build_ninja host_demo
```

## 运行

```bash
./build_ninja/bin/host_demo.exe
```

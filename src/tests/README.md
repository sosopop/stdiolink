# 单元测试

基于 Google Test 的单元测试集合。

## 测试文件

| 文件 | 测试内容 |
|------|----------|
| test_jsonl_serializer.cpp | JSONL 序列化 |
| test_jsonl_parser.cpp | JSONL 解析 |
| test_jsonl_stream_parser.cpp | 流式解析器 |
| test_driver_core.cpp | Driver 核心 |
| test_host_driver.cpp | Host 端 Driver/Task |
| test_wait_any.cpp | 多 Driver 并发 |
| test_console.cpp | Console 模式 |

## 运行测试

```bash
# 运行全部测试
./build/runtime_release/bin/stdiolink_tests.exe

# 运行指定测试
./build/runtime_release/bin/stdiolink_tests.exe --gtest_filter=TestName.*
```

`stdiolink_tests` 启动前会检查自己是否位于发布式 runtime 布局的 `bin/` 目录，且同级存在 `data_root/drivers/` 和 `data_root/services/`。直接运行 `build/release/stdiolink_tests.exe` 这类 raw build 产物会报错退出。

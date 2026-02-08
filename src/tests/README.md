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
./build/bin/stdiolink_tests.exe

# 运行指定测试
./build/bin/stdiolink_tests.exe --gtest_filter=TestName.*
```

# Echo Driver

简单的回显 Driver，接收消息并原样返回。

## 支持的命令

| 命令 | 参数 | 说明 |
|------|------|------|
| echo | msg | 回显消息 |

## 示例

```bash
echo '{"cmd":"echo","data":{"msg":"hello"}}' | ./echo_driver.exe
```

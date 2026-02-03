# Progress Driver

演示事件流的 Driver，发送多个进度事件后完成。

## 支持的命令

| 命令 | 参数 | 说明 |
|------|------|------|
| progress | steps | 发送 N 个进度事件 |

## 示例

```bash
echo '{"cmd":"progress","data":{"steps":3}}' | ./progress_driver.exe
```

输出：
```
{"status":"event","code":0}
{"step":1,"total":3}
{"status":"event","code":0}
{"step":2,"total":3}
{"status":"event","code":0}
{"step":3,"total":3}
{"status":"done","code":0}
{}
```

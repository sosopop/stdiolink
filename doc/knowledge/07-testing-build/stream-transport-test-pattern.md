# Stream Transport Test Pattern

## Purpose

给半包、粘包、分段到达这类流式接收问题一个统一测试入口。

## Pattern

- 先按流式接收问题处理，不要默认把一次读取当成一帧。
- 优先补源码层 GTest；TCP 用假服务端分段回包，串口/管道用测试桩分块喂数据。
- 至少保留两个对照：整帧一次到达通过、同一帧分多次到达也通过或稳定报超时。
- 二进制协议最少再补负向场景：粘包、多帧连发、通道错配、CRC 错、地址不匹配、命令不匹配。
- 只有涉及 runtime、跨进程链路或真机兼容性时，再升级到 smoke。

## Related

- `choose-test-entry.md`
- `triage-test-failure.md`

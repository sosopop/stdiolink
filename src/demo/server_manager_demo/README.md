# Server Manager Demo (M39)

This demo is designed to exercise `stdiolink_server` features from M34-M38.

## Included capabilities

- Service scanning (`/api/services`)
- Project lifecycle and validation (`/api/projects*`)
- Instance lifecycle and logs (`/api/instances*`)
- Driver scanning and refresh (`/api/drivers*`)
- Demo driver staging (copy only, no execution):
  - `stdio.drv.calculator`
  - `stdio.drv.modbusrtu`
  - `stdio.drv.modbustcp`
  - `stdio.drv.3dvision`
- Schedule modes:
  - `manual`
  - `fixed_rate`
  - `daemon` crash-loop suppression

## Layout

```
server_manager_demo/
├── data_root/
│   ├── config.json
│   ├── services/
│   │   └── quick_start_service/
│   ├── projects/
│   │   ├── manual_demo.json
│   │   ├── fixed_rate_demo.json
│   │   └── daemon_demo.json
│   ├── drivers/
│   ├── workspaces/
│   └── logs/
└── scripts/
    ├── run_demo.sh
    └── api_smoke.sh
```

## Quick start

From build output:

```bash
bash ./build/bin/server_manager_demo/scripts/run_demo.sh
```

In another terminal:

```bash
bash ./build/bin/server_manager_demo/scripts/api_smoke.sh
```

Or run directly against custom base URL:

```bash
bash ./build/bin/server_manager_demo/scripts/api_smoke.sh http://127.0.0.1:6200
```

### array<object> 配置演示（M91）

Service `multi_device_service` 专门用于演示 WebUI 对 `array<object>` 类型配置的完整支持：

1. 在 WebUI 的 Services 页面找到"多设备接入服务"
2. 查看 Schema 编辑器：`devices` 字段类型为 `array`，每项含 6 个子字段
3. 在 Projects 页面找到"多设备演示（手动）"，点击"编辑配置"
4. 在配置表单中点击"添加设备"，填写 host / port / driver / enabled，保存
5. 点击 Validate：验证通过则配置合法
6. 在任意 device 中填入未知字段并 Validate：观察错误路径格式 `devices[N].bad_key`

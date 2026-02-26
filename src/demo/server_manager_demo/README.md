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
│   ├── logs/
│   └── shared/
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

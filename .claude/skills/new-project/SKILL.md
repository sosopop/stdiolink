---
name: new-project
description: Create a new Project configuration file for running a Service
argument-hint: <project_name> <service_id> [--schedule <type>]
---

Create a new stdiolink Project configuration.

**Arguments:**
- `$1`: Project display name (required)
- `$2`: Service ID (must exist in services/, required)
- `--schedule`: Schedule type - `manual`, `fixed_rate`, or `daemon` (default: manual)

**Steps:**

1. Parse project_name from `$1` and service_id from `$2` (both required)
2. Generate filename: lowercase project_name, replace spaces with underscores
3. Create file: `src/data_root/projects/{filename}.json`

**For manual schedule:**
```json
{
  "name": "{project_name}",
  "serviceId": "{service_id}",
  "enabled": true,
  "schedule": {
    "type": "manual"
  },
  "config": {}
}
```

**For fixed_rate schedule:**
```json
{
  "name": "{project_name}",
  "serviceId": "{service_id}",
  "enabled": true,
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 60000,
    "maxConcurrent": 1
  },
  "config": {}
}
```

**For daemon schedule:**
```json
{
  "name": "{project_name}",
  "serviceId": "{service_id}",
  "enabled": true,
  "schedule": {
    "type": "daemon",
    "restartDelayMs": 5000,
    "maxConsecutiveFailures": 3
  },
  "config": {}
}
```

4. Replace `{project_name}`, `{service_id}`, `{filename}` with actual values

**After creation, remind user:**
- Fill `config` object based on service's `config.schema.json`
- Start via WebUI or API: `POST /api/projects/{id}/start`

---
name: new-project
description: Create or scaffold a new stdiolink Project JSON under src/data_root/projects for an existing Service, including schedule and config defaults.
argument-hint: <project_name> <service_id> [--schedule manual|fixed_rate|daemon]
---

Use this skill when the user wants a new Project configuration for a Service.

Project files in this repository live under `src/data_root/projects/`.

## Inputs

- Project display name is required.
- Service id is required and should correspond to an existing service directory or manifest.
- Default schedule is `manual`.

## Workflow

1. Inspect the target service first:
   - `src/data_root/services/<service_id>/manifest.json`
   - `src/data_root/services/<service_id>/config.schema.json`
2. Derive the filename from the project name.
   - Use lowercase.
   - Replace spaces with underscores.
   - Prefer `manual_<service_id>.json` when creating a straightforward manual project for a service.
3. Create `src/data_root/projects/<filename>.json`.
4. Fill `config` using schema defaults when available.
5. Keep schedule fields limited to the selected schedule type. Do not invent unsupported fields.

## Schedule Shapes

### Manual

```json
{
  "name": "{project_name}",
  "serviceId": "{service_id}",
  "enabled": false,
  "schedule": { "type": "manual" },
  "config": {}
}
```

### Fixed Rate

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

### Daemon

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

## Validation

- Confirm the service id actually exists.
- Prefer copying config defaults from the service schema instead of leaving an empty object when defaults are known.
- Compare with existing examples in `src/data_root/projects/`.

## References

- `doc/developer-guide.md`
- `src/data_root/projects/`
- `doc/manual/11-server/project-management.md`
- `doc/manual/11-server/instance-and-schedule.md`

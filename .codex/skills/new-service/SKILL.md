---
name: new-service
description: Create or scaffold a new stdiolink JavaScript Service under src/data_root/services, including manifest, config schema, and index.js.
argument-hint: <service_id> [--name <display_name>] [--version <version>] [--keepalive]
---

Use this skill when the user wants a new JS Service in this repository.

Follow the repo's current service layout under `src/data_root/services/`.

## Inputs

- Service id is required.
- Default display name: same as service id.
- Default version: `1.0.0`.
- Default runtime mode: one-shot.
  - If the service should stay alive, use a keepalive loop and do not close the driver immediately.

## Workflow

1. Inspect `src/data_root/services/` for nearby examples first.
2. Create `src/data_root/services/<service_id>/`.
3. Generate:
   - `manifest.json`
   - `config.schema.json`
   - `index.js`
4. If the user provided config requirements, encode them in `config.schema.json` instead of leaving `{}`.
5. If the service targets a known driver, resolve it with `resolveDriver("stdio.drv.xxx")`.
6. If the user wants something runnable immediately, consider also creating a matching project JSON under `src/data_root/projects/`.

## File Patterns

### `manifest.json`

```json
{
  "manifestVersion": "1",
  "id": "{service_id}",
  "name": "{display_name}",
  "version": "{version}",
  "description": "{description}"
}
```

### `config.schema.json`

Use the repository's schema shape, for example:

```json
{
  "listen_port": {
    "type": "int",
    "default": 502,
    "description": "Port to listen on",
    "constraints": { "min": 1, "max": 65535 }
  }
}
```

Do not invent a different schema format.

### `index.js`

Prefer the current module style:

```js
import { getConfig, openDriver } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";

const cfg = getConfig();
const logger = createLogger({ service: "{service_id}" });

(async () => {
    logger.info("service starting", cfg);

    const driverPath = resolveDriver("stdio.drv.echo");
    const drv = await openDriver(driverPath);

    const result = await drv.echo({ msg: "Hello from {service_id}" });
    logger.info("driver response", result);

    await drv.$close();
    logger.info("service completed");
})();
```

For keepalive services, replace the final close with a loop pattern similar to existing server services.

## Validation

- Check against an existing service example before finalizing.
- If practical, run the relevant tests or a minimal runtime check.
- Keep imports aligned with the currently exposed JS bindings in this repo.

## References

- `doc/developer-guide.md`
- `src/data_root/services/modbustcp_server_service/`
- `doc/manual/10-js-service/config-schema.md`

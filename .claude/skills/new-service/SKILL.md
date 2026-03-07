---
name: new-service
description: Create a new JavaScript Service with manifest, schema, and index.js template
argument-hint: <service_id> [--name <name>] [--version <ver>]
---

Create a new stdiolink Service following project conventions.

**Arguments:**
- `$1`: Service ID (directory name, required)
- `--name`: Display name (default: same as service_id)
- `--version`: Version string (default: "1.0.0")

**Steps:**

1. Parse service_id from `$1` (required)
2. Create directory: `src/data_root/services/{service_id}/`
3. Generate three files:

**manifest.json:**
```json
{
  "manifestVersion": "1",
  "id": "{service_id}",
  "name": "{display_name}",
  "version": "{version}"
}
```

**config.schema.json:**
```json
{}
```

**index.js:**
```js
import { getConfig, openDriver } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";

const cfg = getConfig();
const logger = createLogger({ service: "{service_id}" });

(async () => {
    logger.info("service starting", cfg);

    // Open driver using resolveDriver
    const driverPath = resolveDriver("stdio.drv.echo");
    const drv = await openDriver(driverPath);

    // Call driver command
    const result = await drv.echo({ msg: "Hello from {service_id}" });
    logger.info("driver response", result);

    // Clean up (one-shot service)
    await drv.$close();
    logger.info("service completed");
})();
```

4. Replace `{service_id}`, `{display_name}`, `{version}` with actual values

**After creation, remind user:**
- Update `config.schema.json` if service needs configuration parameters
- Test with: `stdiolink_service --service-dir=<path> --data-root=<path>`

import { openDriver } from "stdiolink";
import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { sleep } from "stdiolink/time";
import { resolveDriver } from "stdiolink/driver";

const cfg = getConfig();
const logger = createLogger({ service: "opcua_server_service" });

(async () => {
    const driverPath = resolveDriver("stdio.drv.opcua_server");

    logger.info("starting", {
        bind_host: cfg.bind_host,
        listen_port: cfg.listen_port,
        endpoint_path: cfg.endpoint_path,
        event_mode: cfg.event_mode,
        node_count: Array.isArray(cfg.nodes) ? cfg.nodes.length : 0,
    });

    const drv = await openDriver(driverPath);
    logger.info("driver opened", { driverPath });

    const started = await drv.start_server({
        bind_host: cfg.bind_host,
        listen_port: cfg.listen_port,
        endpoint_path: cfg.endpoint_path,
        server_name: cfg.server_name,
        application_uri: cfg.application_uri,
        namespace_uri: cfg.namespace_uri,
        event_mode: cfg.event_mode,
    });
    logger.info("server started", started);

    const upserted = await drv.upsert_nodes({
        nodes: Array.isArray(cfg.nodes) ? cfg.nodes : [],
    });
    logger.info("nodes loaded", upserted);

    const status = await drv.status();
    logger.info("server ready", status);

    while (true) {
        await sleep(60000);
    }
})();

import { getConfig, openDriver, waitAny } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";
import { sleep } from "stdiolink/time";

const cfg = getConfig();
const logger = createLogger({ service: "plc_crane_sim" });

const runParams = {
    listen_address: String(cfg.listen_address ?? "127.0.0.1"),
    listen_port: Number(cfg.listen_port ?? 1502),
    unit_id: Number(cfg.unit_id ?? 1),
    event_mode: String(cfg.event_mode ?? "write"),
    data_area_size: Number(cfg.data_area_size ?? 256),
    cylinder_up_delay: Number(cfg.cylinder_up_delay ?? 2500),
    cylinder_down_delay: Number(cfg.cylinder_down_delay ?? 2000),
    valve_open_delay: Number(cfg.valve_open_delay ?? 1500),
    valve_close_delay: Number(cfg.valve_close_delay ?? 1200),
    tick_ms: Number(cfg.tick_ms ?? 50),
    heartbeat_ms: Number(cfg.heartbeat_ms ?? 1000),
};

(async () => {
    const driverPath = resolveDriver("stdio.drv.plc_crane_sim");

    logger.info("starting plc_crane_sim service", {
        driverPath,
        runParams
    });

    const driver = await openDriver(driverPath, [], { profilePolicy: "preserve" });
    const runTask = driver.$rawRequest("run", runParams);

    while (true) {
        const next = await waitAny([runTask], 60000);
        if (!next) {
            await sleep(1000);
            continue;
        }

        if (next.msg.status === "event") {
            const payload = next.msg.data ?? {};
            if (payload.event === "sim_heartbeat") {
                logger.debug("heartbeat", payload.data ?? {});
            } else {
                logger.info("driver event", payload);
            }
            continue;
        }

        if (next.msg.status === "error") {
            logger.error("run failed", next.msg.data ?? {});
            throw new Error("plc_crane_sim run failed");
        }

        logger.warn("run finished unexpectedly", next.msg);
        break;
    }
})();

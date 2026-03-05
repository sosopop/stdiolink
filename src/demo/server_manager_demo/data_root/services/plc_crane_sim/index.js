import { getConfig, openDriver, waitAny } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";
import { sleep } from "stdiolink/time";

const cfg = getConfig();
const logger = createLogger({ service: "plc_crane_sim" });

const listenAddress = String(cfg.listenAddress ?? "127.0.0.1");
const listenPort = Number(cfg.listenPort ?? 1502);
const unitId = Number(cfg.unitId ?? 1);
const eventMode = String(cfg.eventMode ?? "write");
const dataAreaSize = Number(cfg.dataAreaSize ?? 256);
const cylinderUpDelayMs = Number(cfg.cylinderUpDelayMs ?? 2500);
const cylinderDownDelayMs = Number(cfg.cylinderDownDelayMs ?? 2000);
const valveOpenDelayMs = Number(cfg.valveOpenDelayMs ?? 1500);
const valveCloseDelayMs = Number(cfg.valveCloseDelayMs ?? 1200);
const tickMs = Number(cfg.tickMs ?? 50);
const heartbeatMs = Number(cfg.heartbeatMs ?? 1000);

function buildDriverArgs() {
    return [
        `--listen-address=${listenAddress}`,
        `--listen-port=${listenPort}`,
        `--unit-id=${unitId}`,
        `--event-mode=${eventMode}`,
        `--data-area-size=${dataAreaSize}`,
        `--cylinder-up-delay=${cylinderUpDelayMs}`,
        `--cylinder-down-delay=${cylinderDownDelayMs}`,
        `--valve-open-delay=${valveOpenDelayMs}`,
        `--valve-close-delay=${valveCloseDelayMs}`,
        `--tick-ms=${tickMs}`,
        `--heartbeat-ms=${heartbeatMs}`,
    ];
}

(async () => {
    const driverPath = resolveDriver("stdio.drv.plc_crane_sim");
    const args = buildDriverArgs();

    logger.info("starting plc_crane_sim service", {
        driverPath,
        listenAddress,
        listenPort,
        unitId,
        eventMode
    });

    const driver = await openDriver(driverPath, args, { profilePolicy: "preserve" });
    const runTask = driver.$rawRequest("run", {});

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


import { Driver, getConfig } from "stdiolink";
import { resolveDriver } from "stdiolink/driver";
import { writeJson } from "stdiolink/fs";
import { createLogger } from "stdiolink/log";
import { sleep } from "stdiolink/time";

const cfg = getConfig();
const logger = createLogger({ service: "bin_scan_orchestrator" });

const PLC_DRIVER_NAME = "stdio.drv.plc_crane";
const VISION_DRIVER_NAME = "stdio.drv.3dvision";

function numberOr(value, fallback) {
    return Number.isFinite(Number(value)) ? Number(value) : fallback;
}

function craneParams(craneCfg) {
    return {
        host: String(craneCfg.host),
        port: numberOr(craneCfg.port, 502),
        unit_id: numberOr(craneCfg.unit_id, 1),
        timeout: numberOr(craneCfg.timeout_ms, 3000)
    };
}

function isCraneReady(status) {
    return status.cylinder_down === true && status.valve_open === true;
}

function isFreshLog(logInfo, scanStartedAt, toleranceMs) {
    const logTimeMs = Date.parse(String(logInfo?.logTime ?? ""));
    if (!Number.isFinite(logTimeMs)) {
        return false;
    }
    return logTimeMs > (scanStartedAt - toleranceMs);
}

function describeError(err) {
    if (err instanceof Error) {
        return err.message;
    }
    return String(err);
}

function buildResult(config, scanStartedAt, visionLog) {
    const scanCompletedAt = new Date().toISOString();
    return {
        vesselId: Number(config.vessel_id),
        status: "success",
        scanStartedAt: new Date(scanStartedAt).toISOString(),
        scanCompletedAt,
        scanDurationMs: Date.parse(scanCompletedAt) - scanStartedAt,
        completionChannel: "poll",
        visionLog,
        cranes: (config.cranes ?? []).map((crane) => ({
            id: String(crane.id),
            host: String(crane.host),
            unitId: numberOr(crane.unit_id, 1)
        }))
    };
}

function buildDriverArgs(args) {
    const src = Array.isArray(args) ? args : [];
    return src.some((item) => item.startsWith("--profile="))
        ? src.slice()
        : [...src, "--profile=keepalive"];
}

function commandTimeout(timeoutMs) {
    if (timeoutMs == null) {
        return 0;
    }
    if (!Number.isInteger(timeoutMs) || timeoutMs < 0) {
        throw new RangeError("timeoutMs must be a non-negative integer");
    }
    return timeoutMs;
}

function terminalError(task, cmd) {
    const err = new Error(task.errorText || `driver exited during command: ${cmd}`);
    err.code = task.exitCode;
    return err;
}

async function waitTaskDone(driver, cmd, task, timeoutMs) {
    const deadline = timeoutMs > 0 ? Date.now() + timeoutMs : 0;

    while (true) {
        const waitMs = timeoutMs > 0 ? Math.max(0, deadline - Date.now()) : -1;
        const msg = task.waitNext(waitMs);
        if (!msg) {
            if (task.done) {
                throw terminalError(task, cmd);
            }
            if (timeoutMs > 0) {
                driver.terminate();
                const err = new Error(`Command timeout: ${cmd} (${timeoutMs}ms)`);
                err.code = "ETIMEDOUT";
                throw err;
            }
            throw new Error(`Command ended without terminal response: ${cmd}`);
        }
        if (msg.status === "event") {
            continue;
        }
        if (msg.status === "error") {
            const data = (msg.data && typeof msg.data === "object") ? msg.data : {};
            const err = new Error(data.message || `Command failed: ${cmd}`);
            err.code = msg.code;
            err.data = msg.data;
            throw err;
        }
        if (msg.status === "done") {
            return msg.data;
        }

        driver.terminate();
        throw new Error(`Unexpected task status: ${msg.status}`);
    }
}

async function openKnownDriver(driverName, commandNames, args = []) {
    const program = resolveDriver(driverName);
    const driver = new Driver();
    if (!driver.start(program, buildDriverArgs(args))) {
        throw new Error(`Failed to start driver: ${program}`);
    }

    let busy = false;
    const proxy = {
        $driver: driver,
        $close() {
            driver.terminate();
        }
    };

    for (const commandName of commandNames) {
        proxy[commandName] = async (params = {}, options) => {
            if (busy) {
                throw new Error("DriverBusyError: request already in flight");
            }
            busy = true;
            try {
                const timeoutMs = commandTimeout(options?.timeoutMs ?? 0);
                const task = driver.request(commandName, params);
                return await waitTaskDone(driver, commandName, task, timeoutMs);
            } finally {
                busy = false;
            }
        };
    }

    return proxy;
}

async function writeResultFile(config, result) {
    const outputPath = String(config.result_output_path ?? "").trim();
    if (!outputPath) {
        return;
    }
    writeJson(outputPath, result, { ensureParent: true });
    logger.info("result written", { path: outputPath });
}

async function openVision(visionCfg) {
    const driver = await openKnownDriver(VISION_DRIVER_NAME, [
        "login",
        "vessel.command",
        "vessellog.last"
    ]);
    try {
        const loginResult = await driver.login(
            {
                addr: String(visionCfg.addr),
                userName: String(visionCfg.user_name),
                password: String(visionCfg.password),
                viewMode: Boolean(visionCfg.view_mode)
            },
            { timeoutMs: numberOr(cfg.scan_request_timeout_ms, 8000) }
        );
        const token = String(loginResult?.token ?? "");
        if (!token) {
            throw new Error("vision login returned empty token");
        }
        logger.info("vision login success", { addr: String(visionCfg.addr) });
        return { driver, token };
    } catch (err) {
        try {
            driver.$close();
        } catch (_) {
        }
        throw err;
    }
}

async function openCranes(craneCfgList) {
    const out = [];
    try {
        for (const craneCfg of craneCfgList) {
            const driver = await openKnownDriver(PLC_DRIVER_NAME, [
                "set_mode",
                "read_status"
            ]);
            out.push({ cfg: craneCfg, driver });
        }
        logger.info("cranes opened", { count: out.length });
        return out;
    } catch (err) {
        for (const item of out) {
            try {
                item.driver.$close();
            } catch (_) {
            }
        }
        throw err;
    }
}

async function prepareCranes(cranes) {
    await Promise.all(
        cranes.map(({ cfg: craneCfg, driver }) =>
            driver.set_mode({
                ...craneParams(craneCfg),
                mode: "auto"
            })
        )
    );
    logger.info("cranes set to auto", { count: cranes.length });
}

async function waitCranesReady(cranes, config) {
    const deadline = Date.now() + numberOr(config.crane_wait_timeout_ms, 60000);
    const pollIntervalMs = numberOr(config.crane_poll_interval_ms, 1000);
    let round = 0;

    while (true) {
        round += 1;
        const statuses = await Promise.all(
            cranes.map(({ cfg: craneCfg, driver }) => driver.read_status(craneParams(craneCfg)))
        );
        logger.info("crane poll round", {
            round,
            readyCount: statuses.filter(isCraneReady).length,
            total: statuses.length
        });

        if (statuses.every(isCraneReady)) {
            logger.info("cranes ready", { rounds: round });
            return statuses;
        }
        if (Date.now() >= deadline) {
            throw new Error("crane wait timeout");
        }

        await sleep(pollIntervalMs);
    }
}

async function startScan(visionDriver, config, token) {
    const maxAttempts = numberOr(config.scan_start_retry_count, 2) + 1;
    const retryIntervalMs = numberOr(config.scan_start_retry_interval_ms, 1000);
    const timeoutMs = numberOr(config.scan_request_timeout_ms, 8000);

    for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
        try {
            await visionDriver["vessel.command"](
                {
                    addr: String(config.vision.addr),
                    token,
                    id: Number(config.vessel_id),
                    cmd: "scan"
                },
                { timeoutMs }
            );
            logger.info("scan start success", { attempt });
            return Date.now();
        } catch (err) {
            if (attempt >= maxAttempts) {
                throw new Error(`scan start failed after ${maxAttempts} attempts: ${describeError(err)}`);
            }
            logger.warn("scan start retry", {
                attempt,
                error: describeError(err)
            });
            if (retryIntervalMs > 0) {
                await sleep(retryIntervalMs);
            }
        }
    }

    throw new Error("unreachable");
}

async function pollScanCompleted(visionDriver, config, scanStartedAt) {
    const pollIntervalMs = numberOr(config.scan_poll_interval_ms, 3000);
    const failLimit = numberOr(config.scan_poll_fail_limit, 5);
    const timeoutMs = numberOr(config.scan_request_timeout_ms, 8000);
    const scanDeadline = scanStartedAt + numberOr(config.scan_timeout_ms, 120000);
    const toleranceMs = numberOr(config.clock_skew_tolerance_ms, 2000);
    let consecutiveFailures = 0;
    let polls = 0;

    while (true) {
        if (Date.now() >= scanDeadline) {
            throw new Error("scan timeout");
        }

        polls += 1;
        try {
            const logInfo = await visionDriver["vessellog.last"](
                {
                    addr: String(config.vision.addr),
                    id: Number(config.vessel_id)
                },
                { timeoutMs }
            );

            if (isFreshLog(logInfo, scanStartedAt, toleranceMs)) {
                logger.info("scan completed", { polls, vesselId: Number(config.vessel_id) });
                return logInfo;
            }

            consecutiveFailures = 0;
            logger.info("scan poll stale log", { polls });
        } catch (err) {
            consecutiveFailures += 1;
            logger.warn("scan poll failed", {
                polls,
                consecutiveFailures,
                error: describeError(err)
            });
            if (consecutiveFailures >= failLimit) {
                throw new Error(`scan poll fail limit reached: ${describeError(err)}`);
            }
        }

        const remainingMs = scanDeadline - Date.now();
        if (remainingMs <= 0) {
            throw new Error("scan timeout");
        }
        await sleep(Math.min(pollIntervalMs, remainingMs));
    }
}

async function safeSetManual(cranes) {
    const settled = await Promise.allSettled(
        cranes.map(({ cfg: craneCfg, driver }) =>
            driver.set_mode({
                ...craneParams(craneCfg),
                mode: "manual"
            })
        )
    );

    const failures = settled
        .map((item, index) => ({ item, index }))
        .filter(({ item }) => item.status === "rejected")
        .map(({ item, index }) => ({
            id: String(cranes[index].cfg.id),
            error: describeError(item.reason)
        }));

    if (failures.length > 0) {
        logger.warn("set manual partial failure", { failures });
    } else {
        logger.info("cranes set to manual", { count: cranes.length });
    }
}

function closeAll(cranes, visionDriver) {
    for (const crane of cranes) {
        try {
            crane.driver.$close();
        } catch (_) {
        }
    }
    if (visionDriver) {
        try {
            visionDriver.$close();
        } catch (_) {
        }
    }
}

(async () => {
    let visionDriver = null;
    let cranes = [];

    try {
        const vision = await openVision(cfg.vision);
        visionDriver = vision.driver;
        cranes = await openCranes(cfg.cranes ?? []);

        await prepareCranes(cranes);
        await waitCranesReady(cranes, cfg);
        const scanStartedAt = await startScan(visionDriver, cfg, vision.token);
        const visionLog = await pollScanCompleted(visionDriver, cfg, scanStartedAt);
        const result = buildResult(cfg, scanStartedAt, visionLog);
        await writeResultFile(cfg, result);
        logger.info("scan workflow finished", {
            vesselId: Number(cfg.vessel_id),
            scanDurationMs: result.scanDurationMs
        });
    } catch (err) {
        if (Boolean(cfg.on_error_set_manual) && cranes.length > 0) {
            await safeSetManual(cranes);
        }
        logger.error("scan failed", { message: describeError(err) });
        throw err;
    } finally {
        closeAll(cranes, visionDriver);
    }
})();

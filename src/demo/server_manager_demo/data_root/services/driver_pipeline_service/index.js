/**
 * driver_pipeline_service — Driver 编排演示
 *
 * 覆盖绑定: openDriver, waitAny, time, log, config, fs, path, constants
 */

import { openDriver, waitAny } from "stdiolink";
import { getConfig } from "stdiolink";
import { SYSTEM } from "stdiolink/constants";
import { join, resolve } from "stdiolink/path";
import { exists, mkdir, writeJson } from "stdiolink/fs";
import { createLogger } from "stdiolink/log";
import { nowMs, monotonicMs } from "stdiolink/time";
import { resolveDriver } from "stdiolink/driver";

const cfg = getConfig();
const iterations = Number(cfg.iterations ?? 3);
const outputDir = String(cfg.outputDir ?? "./output");

const logger = createLogger({ service: "driver_pipeline" });

async function openCalc() {
    const driverPath = resolveDriver("stdio.drv.calculator");
    return await openDriver(driverPath);
}

(async () => {
    const t0 = monotonicMs();
    logger.info("start", { iterations, outputDir, os: SYSTEM.os });

    const results = { proxy: [], batch: [], waitAny: [] };

    // --- openDriver + Proxy 语法演示 ---
    const calc = await openCalc();
    logger.info("driver opened");

    for (let i = 0; i < iterations; i++) {
        const a = (i + 1) * 10;
        const b = (i + 1) * 3;

        const addResult = await calc.add({ a, b });
        const mulResult = await calc.multiply({ a, b });

        results.proxy.push({
            iteration: i + 1,
            add: addResult.result,
            multiply: mulResult.result,
        });
        logger.info(`iteration ${i + 1}`, { add: addResult.result, mul: mulResult.result });
    }

    // --- $rawRequest + waitAny 演示: batch 并发 ---
    const calc2 = await openCalc();
    logger.info("second driver opened for waitAny demo");

    const taskA = calc.$rawRequest("batch", {
        operations: [
            { type: "add", a: 1, b: 2 },
            { type: "mul", a: 3, b: 4 },
            { type: "sub", a: 10, b: 3 },
        ],
    });
    const taskB = calc2.$rawRequest("batch", {
        operations: [
            { type: "add", a: 100, b: 200 },
            { type: "div", a: 50, b: 5 },
        ],
    });

    let doneCount = 0;
    while (doneCount < 2) {
        const result = await waitAny([taskA, taskB], 10000);
        if (!result) {
            logger.warn("waitAny returned null (timeout or all done)");
            break;
        }

        const entry = {
            taskIndex: result.taskIndex,
            status: result.msg.status,
            data: result.msg.data,
        };

        if (result.msg.status === "event") {
            results.waitAny.push(entry);
            continue;
        }
        if (result.msg.status === "done") {
            doneCount++;
            results.batch.push(entry);
            logger.info(`batch task#${result.taskIndex} done`, { data: result.msg.data });
            continue;
        }
        logger.warn(`batch task#${result.taskIndex} error`, { data: result.msg.data });
        break;
    }

    calc.$close();
    calc2.$close();
    logger.info("drivers closed");

    // --- 写 JSON 报告 ---
    const outBase = resolve(outputDir);
    if (!exists(outBase)) {
        mkdir(outBase, { recursive: true });
    }
    const reportFile = join(outBase, "driver_pipeline_report.json");
    const report = {
        timestamp: nowMs(),
        elapsedMs: monotonicMs() - t0,
        iterations,
        results,
    };
    writeJson(reportFile, report, { ensureParent: true });
    logger.info("done", { elapsedMs: report.elapsedMs, reportFile });
})();

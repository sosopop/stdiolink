/**
 * system_info_service — 系统信息收集
 *
 * 覆盖绑定: constants, path, fs, log, config, time
 */

import { getConfig } from "stdiolink";
import { SYSTEM, APP_PATHS } from "stdiolink/constants";
import { join, resolve, dirname, basename, extname } from "stdiolink/path";
import { exists, mkdir, writeJson, readJson, writeText, listDir } from "stdiolink/fs";
import { createLogger } from "stdiolink/log";
import { nowMs, monotonicMs, sleep } from "stdiolink/time";

const cfg = getConfig();
const outputDir = String(cfg.outputDir ?? "./output");
const reportName = String(cfg.reportName ?? "system_report");
const verbose = Boolean(cfg.verbose);

const logger = createLogger({ service: "system_info" });

(async () => {
    const t0 = monotonicMs();
    logger.info("start", { outputDir, reportName, verbose });

    // --- constants 演示 ---
    const sysInfo = {
        os: SYSTEM.os,
        arch: SYSTEM.arch,
        isWindows: SYSTEM.isWindows,
        isMac: SYSTEM.isMac,
        isLinux: SYSTEM.isLinux,
    };
    const pathInfo = {
        appDir: APP_PATHS.appDir,
        cwd: APP_PATHS.cwd,
        serviceDir: APP_PATHS.serviceDir,
        tempDir: APP_PATHS.tempDir,
    };
    if (verbose) {
        logger.debug("SYSTEM", sysInfo);
        logger.debug("APP_PATHS", pathInfo);
    }

    // --- path 演示 ---
    const outBase = resolve(outputDir);
    const reportFile = join(outBase, reportName + ".json");
    const reportDir = dirname(reportFile);
    const reportBase = basename(reportFile);
    const reportExt = extname(reportFile);
    logger.info("paths computed", { reportFile, reportDir, reportBase, reportExt });

    // --- fs 演示 ---
    if (!exists(outBase)) {
        mkdir(outBase, { recursive: true });
        logger.info("created output dir", { path: outBase });
    }

    // --- time 演示 ---
    const timestamp = nowMs();
    await sleep(50); // 短暂等待演示 async sleep
    const afterSleep = monotonicMs();

    const report = {
        generatedAt: timestamp,
        system: sysInfo,
        paths: pathInfo,
        computedPaths: { outBase, reportFile, reportDir, reportBase, reportExt },
        timing: {
            sleepDemoMs: afterSleep - (t0 + (afterSleep - monotonicMs() + (afterSleep - t0))),
        },
    };

    // --- fs write/read 演示 ---
    writeJson(reportFile, report, { ensureParent: true });
    logger.info("report written", { path: reportFile });

    const readBack = readJson(reportFile);
    logger.info("report verified", { os: readBack.system.os });

    // 写一份文本摘要
    const summaryFile = join(outBase, reportName + "_summary.txt");
    const summaryText = [
        `System Report: ${reportName}`,
        `OS: ${sysInfo.os} (${sysInfo.arch})`,
        `Generated: ${new Date(timestamp).toISOString()}`,
        `Keys in report: ${Object.keys(report).length}`,
    ].join("\n");
    writeText(summaryFile, summaryText, { ensureParent: true });

    // --- fs listDir 演示 ---
    const files = listDir(outBase, { filesOnly: true });
    logger.info("output directory listing", { count: files.length, files });

    const elapsed = monotonicMs() - t0;
    logger.info("done", { elapsedMs: elapsed });
})();

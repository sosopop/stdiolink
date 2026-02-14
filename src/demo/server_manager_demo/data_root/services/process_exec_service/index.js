/**
 * process_exec_service — 进程执行演示
 *
 * 覆盖绑定: process_async(execAsync+spawn), constants, path, fs, log, config, time
 */

import { getConfig } from "stdiolink";
import { SYSTEM, APP_PATHS } from "stdiolink/constants";
import { join, resolve } from "stdiolink/path";
import { exists, mkdir, writeJson } from "stdiolink/fs";
import { createLogger } from "stdiolink/log";
import { nowMs, monotonicMs } from "stdiolink/time";
import { execAsync, spawn } from "stdiolink/process";

const cfg = getConfig();
const outputDir = String(cfg.outputDir ?? "./output");
const spawnDriver = String(cfg.spawnDriver ?? "calculator_driver");

const logger = createLogger({ service: "process_exec" });

// 探测 driver 路径
function findDriver(baseName) {
    const ext = SYSTEM.isWindows ? ".exe" : "";
    const name = baseName + ext;
    const candidates = [
        `./${name}`, `./bin/${name}`, `../bin/${name}`,
        `../../bin/${name}`, `./build/bin/${name}`,
    ];
    for (const p of candidates) {
        if (exists(p)) return p;
    }
    return null;
}

(async () => {
    const t0 = monotonicMs();
    logger.info("start", { outputDir, spawnDriver, os: SYSTEM.os });

    const results = {};

    // --- execAsync 演示 1: 运行系统命令 ---
    try {
        const echoCmd = SYSTEM.isWindows ? "cmd" : "echo";
        const echoArgs = SYSTEM.isWindows ? ["/c", "echo hello from execAsync"] : ["hello from execAsync"];
        const echoResult = await execAsync(echoCmd, echoArgs, { timeoutMs: 5000 });
        results.echo = {
            exitCode: echoResult.exitCode,
            stdout: echoResult.stdout.trim(),
        };
        logger.info("execAsync echo", results.echo);
    } catch (e) {
        results.echo = { error: String(e) };
        logger.warn("execAsync echo failed", { error: String(e) });
    }

    // --- execAsync 演示 2: 运行 dir/ls ---
    try {
        const listCmd = SYSTEM.isWindows ? "cmd" : "ls";
        const listArgs = SYSTEM.isWindows ? ["/c", "dir", "/b", APP_PATHS.cwd] : ["-la", APP_PATHS.cwd];
        const listResult = await execAsync(listCmd, listArgs, { timeoutMs: 5000 });
        const lines = listResult.stdout.trim().split("\n").filter(Boolean);
        results.dirList = {
            exitCode: listResult.exitCode,
            fileCount: lines.length,
            firstFiles: lines.slice(0, 5),
        };
        logger.info("execAsync dir", { fileCount: lines.length });
    } catch (e) {
        results.dirList = { error: String(e) };
        logger.warn("execAsync dir failed", { error: String(e) });
    }

    // --- execAsync 演示 3: 运行 driver --export-meta ---
    const driverPath = findDriver(spawnDriver);
    if (driverPath) {
        try {
            const metaResult = await execAsync(driverPath, ["--export-meta"], { timeoutMs: 5000 });
            const metaJson = JSON.parse(metaResult.stdout.trim());
            results.driverMeta = {
                id: metaJson.id,
                name: metaJson.name,
                commandCount: metaJson.commands ? metaJson.commands.length : 0,
            };
            logger.info("driver meta exported", results.driverMeta);
        } catch (e) {
            results.driverMeta = { error: String(e) };
            logger.warn("driver meta export failed", { error: String(e) });
        }

        // --- spawn 演示: 启动 driver 并交互 ---
        try {
            results.spawn = await new Promise((resolve, reject) => {
                const collected = [];
                const proc = spawn(driverPath, ["--profile", "oneshot"]);

                proc.onStdout((data) => {
                    const trimmed = data.trim();
                    if (trimmed) {
                        try {
                            collected.push(JSON.parse(trimmed));
                        } catch (_) {
                            collected.push({ raw: trimmed });
                        }
                    }
                });

                proc.onExit((info) => {
                    resolve({
                        exitCode: info.exitCode,
                        exitStatus: info.exitStatus,
                        responses: collected,
                    });
                });

                // 发送一个计算请求
                const request = JSON.stringify({ cmd: "add", data: { a: 42, b: 58 } });
                proc.write(request + "\n");
                proc.closeStdin();
            });
            logger.info("spawn interaction done", {
                exitCode: results.spawn.exitCode,
                responseCount: results.spawn.responses.length,
            });
        } catch (e) {
            results.spawn = { error: String(e) };
            logger.warn("spawn failed", { error: String(e) });
        }
    } else {
        results.driverMeta = { skipped: true, reason: `${spawnDriver} not found` };
        results.spawn = { skipped: true, reason: `${spawnDriver} not found` };
        logger.warn("driver not found, skipping meta/spawn tests", { driver: spawnDriver });
    }

    // --- 写结果到文件 ---
    const outBase = resolve(outputDir);
    if (!exists(outBase)) {
        mkdir(outBase, { recursive: true });
    }
    const reportFile = join(outBase, "process_exec_report.json");
    const report = {
        timestamp: nowMs(),
        elapsedMs: monotonicMs() - t0,
        system: { os: SYSTEM.os, arch: SYSTEM.arch },
        results,
    };
    writeJson(reportFile, report, { ensureParent: true });
    logger.info("done", { elapsedMs: report.elapsedMs, reportFile });
})();

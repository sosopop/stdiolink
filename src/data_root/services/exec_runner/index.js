import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { spawn } from "stdiolink/process";

const cfg = getConfig();
const logger = createLogger({ service: "exec_runner" });

// Module-level reference prevents GC from finalizing the spawn handle
// while the process is still running.
let _spawnHandle = null;

function normalizeArgs(value) {
    return Array.isArray(value) ? value.map((v) => String(v)) : [];
}

function normalizeEnv(value) {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        return undefined;
    }

    const env = {};
    for (const [key, envValue] of Object.entries(value)) {
        env[String(key)] = String(envValue ?? "");
    }
    return env;
}

function normalizeSuccessCodes(value) {
    if (!Array.isArray(value) || value.length === 0) {
        return new Set([0]);
    }

    const normalized = value
        .map((v) => Number(v))
        .filter((v) => Number.isInteger(v));
    return normalized.length > 0 ? new Set(normalized) : new Set([0]);
}

function runProcess() {
    const program = String(cfg.program ?? "");
    if (!program) {
        throw new Error("program is required");
    }

    const args = normalizeArgs(cfg.args);
    const env = normalizeEnv(cfg.env);
    const cwd = String(cfg.cwd ?? "");
    const successCodes = normalizeSuccessCodes(cfg.success_exit_codes);
    const logStdout = cfg.log_stdout !== false;
    const logStderr = cfg.log_stderr !== false;

    return new Promise((resolve, reject) => {
        _spawnHandle = spawn(program, args, {
            cwd: cwd || undefined,
            env,
        });

        logger.info("exec_runner start", { program, args });

        if (logStdout) {
            _spawnHandle.onStdout((chunk) => logger.info("stdout", { data: chunk }));
        }
        if (logStderr) {
            _spawnHandle.onStderr((chunk) => logger.warn("stderr", { data: chunk }));
        }

        _spawnHandle.onExit((info) => {
            logger.info("exec_runner exit", info);
            if (info.exitStatus === "crash") {
                reject(new Error("process crashed"));
                return;
            }
            if (!successCodes.has(info.exitCode)) {
                reject(new Error(`process exited with code ${info.exitCode}`));
                return;
            }
            resolve();
        });
    });
}

async function main() {
    await runProcess();
    logger.info("exec_runner completed successfully");
}

main().catch((error) => {
    logger.error("exec_runner failed", {
        message: error?.message ?? String(error),
    });
    throw error;
});

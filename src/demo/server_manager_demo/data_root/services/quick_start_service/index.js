import { getConfig } from "stdiolink";

const cfg = getConfig();
const mode = String(cfg.mode ?? "normal");
const runMs = Number(cfg.runMs ?? 0);
const message = String(cfg.message ?? "");

console.log(
    `[server_manager_demo] start mode=${mode} runMs=${runMs} message=${message}`
);

const begin = Date.now();
while (Date.now() - begin < runMs) {
}

if (mode === "crash") {
    throw new Error("[server_manager_demo] crash mode requested");
}

console.log("[server_manager_demo] finished successfully");

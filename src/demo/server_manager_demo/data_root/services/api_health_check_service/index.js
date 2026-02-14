/**
 * api_health_check_service — API 健康检查
 *
 * 覆盖绑定: http, time, log, config, constants
 */

import { getConfig } from "stdiolink/config";
import { SYSTEM } from "stdiolink/constants";
import { get, post, request } from "stdiolink/http";
import { createLogger } from "stdiolink/log";
import { nowMs, monotonicMs } from "stdiolink/time";

const cfg = getConfig();
const baseUrl = String(cfg.baseUrl ?? "http://127.0.0.1:18080");
const timeoutMs = Number(cfg.timeoutMs ?? 5000);
const checkDrivers = cfg.checkDrivers !== false;

const logger = createLogger({ service: "api_health_check" });

async function checkEndpoint(method, path, body) {
    const url = baseUrl + path;
    const t0 = monotonicMs();
    try {
        let resp;
        if (method === "GET") {
            resp = await get(url, { timeoutMs });
        } else if (method === "POST") {
            resp = await post(url, body, { timeoutMs });
        } else {
            resp = await request({ url, method, timeoutMs });
        }
        const latency = monotonicMs() - t0;
        const ok = resp.status >= 200 && resp.status < 300;
        logger.info(`${method} ${path}`, { status: resp.status, latencyMs: latency, ok });
        return { path, method, status: resp.status, ok, latencyMs: latency };
    } catch (e) {
        const latency = monotonicMs() - t0;
        logger.warn(`${method} ${path} failed`, { error: String(e), latencyMs: latency });
        return { path, method, status: 0, ok: false, latencyMs: latency, error: String(e) };
    }
}

(async () => {
    const t0 = monotonicMs();
    logger.info("start health check", { baseUrl, timeoutMs, checkDrivers, os: SYSTEM.os });

    const results = [];

    // GET 端点检查
    results.push(await checkEndpoint("GET", "/api/services"));
    results.push(await checkEndpoint("GET", "/api/projects"));
    results.push(await checkEndpoint("GET", "/api/instances"));

    if (checkDrivers) {
        results.push(await checkEndpoint("GET", "/api/drivers"));
    }

    // POST 触发扫描
    results.push(await checkEndpoint("POST", "/api/services/scan"));
    if (checkDrivers) {
        results.push(await checkEndpoint("POST", "/api/drivers/scan"));
    }

    // request() 自定义请求演示
    results.push(await checkEndpoint("GET", "/api/services"));

    // 汇总
    const passed = results.filter(r => r.ok).length;
    const failed = results.filter(r => !r.ok).length;
    const totalMs = monotonicMs() - t0;

    logger.info("health check complete", {
        total: results.length,
        passed,
        failed,
        totalMs,
        timestamp: nowMs(),
    });

    if (failed > 0) {
        logger.warn("some checks failed", {
            failedEndpoints: results.filter(r => !r.ok).map(r => r.path),
        });
    }
})();

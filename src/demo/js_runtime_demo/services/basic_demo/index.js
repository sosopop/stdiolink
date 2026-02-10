import { exec } from "stdiolink";

// All-in-one demo: runs each service directory sequentially

function findService() {
    const candidates = [
        "./stdiolink_service.exe",
        "./stdiolink_service",
        "./build/bin/stdiolink_service.exe",
        "./build/bin/stdiolink_service",
        "stdiolink_service.exe",
        "stdiolink_service"
    ];
    for (const c of candidates) {
        try {
            exec(c, ["--help"], { timeout: 3000 });
            return c;
        } catch (e) {
            // try next
        }
    }
    throw new Error("Cannot find stdiolink_service executable");
}

function findServiceBase(service) {
    const bases = [
        "js_runtime_demo/services",
        "src/demo/js_runtime_demo/services"
    ];
    for (const base of bases) {
        try {
            const probe = exec(service, [`${base}/engine_modules`, "--help"], { timeout: 5000 });
            if (probe.exitCode === 0) {
                return base;
            }
        } catch (e) {
            // try next
        }
    }
    throw new Error("Cannot find js_runtime_demo services directory");
}

const service = findService();
const serviceBase = findServiceBase(service);
const services = [
    `${serviceBase}/engine_modules`,
    `${serviceBase}/driver_task`,
    `${serviceBase}/proxy_scheduler`,
    `${serviceBase}/wait_any`,
    `${serviceBase}/process_types`
];

console.log("=== JS runtime demo start (M21-M33) ===");

for (const svc of services) {
    console.log("--- Running:", svc, "---");
    const result = exec(service, [svc], { timeout: 30000 });
    if (result.stderr) {
        console.log(result.stderr.trim());
    }
    if (result.exitCode !== 0) {
        console.log("EXIT CODE:", result.exitCode);
    }
}

console.log("=== JS runtime demo done (M21-M33) ===");

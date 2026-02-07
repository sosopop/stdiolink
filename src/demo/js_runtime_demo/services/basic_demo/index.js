import { exec } from "stdiolink";

// All-in-one demo: runs each service directory sequentially

function findService() {
    const candidates = [
        "./stdiolink_service.exe",
        "./stdiolink_service"
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

const service = findService();
const services = [
    "js_runtime_demo/services/engine_modules",
    "js_runtime_demo/services/driver_task",
    "js_runtime_demo/services/proxy_scheduler",
    "js_runtime_demo/services/process_types"
];

console.log("=== JS runtime demo start (M21-M27) ===");

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

console.log("=== JS runtime demo done (M21-M27) ===");

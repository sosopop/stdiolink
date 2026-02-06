import { buildReport } from "../modules/math/stat.js";

export async function run() {
    const report = buildReport(3, 4);
    console.log("[M21/M22] report:", JSON.stringify(report));
    console.warn("[M21] console.warn bridge is active");
    console.error("[M21] console.error bridge is active");
}

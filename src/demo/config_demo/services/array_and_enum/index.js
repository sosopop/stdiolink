import { getConfig } from "stdiolink";

// M28 场景4：数组与枚举

const cfg = getConfig();

console.log("[M28] === 04_array_and_enum ===");
console.log("[M28] mode:", cfg.mode, "(enum: fast|normal|slow|debug)");
console.log("[M28] tags:", JSON.stringify(cfg.tags), "(length:", cfg.tags.length + ")");
console.log("[M28] ports:", JSON.stringify(cfg.ports), "(length:", cfg.ports.length + ")");
console.log("[M28] level:", cfg.level);
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

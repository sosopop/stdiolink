import { defineConfig, getConfig } from "stdiolink";

// M28 场景4：数组与枚举
// 演示 enum 类型（enumValues 约束）和 array 类型（含 items 约束）

defineConfig({
    mode: {
        type: "string",
        default: "normal",
        description: "Run mode",
        constraints: { enumValues: ["fast", "normal", "slow", "debug"] }
    },
    tags: {
        type: "array",
        default: [],
        description: "Tags list",
        items: { type: "string" },
        constraints: { maxItems: 10 }
    },
    ports: {
        type: "array",
        default: [80],
        description: "Listen ports",
        items: { type: "int", constraints: { min: 1, max: 65535 } }
    },
    level: {
        type: "int",
        required: true,
        description: "Priority level"
    }
});

const cfg = getConfig();

console.log("[M28] === 04_array_and_enum ===");
console.log("[M28] mode:", cfg.mode, "(enum: fast|normal|slow|debug)");
console.log("[M28] tags:", JSON.stringify(cfg.tags), "(length:", cfg.tags.length + ")");
console.log("[M28] ports:", JSON.stringify(cfg.ports), "(length:", cfg.ports.length + ")");
console.log("[M28] level:", cfg.level);
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

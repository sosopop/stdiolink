import { defineConfig, getConfig } from "stdiolink";

// M28 场景2：约束校验
// 演示 min/max, minLength/maxLength, pattern, maxItems 等约束

defineConfig({
    port: {
        type: "int",
        required: true,
        description: "Listen port (1-65535)",
        constraints: { min: 1, max: 65535 }
    },
    name: {
        type: "string",
        required: true,
        description: "Service name (identifier format)",
        constraints: { minLength: 1, maxLength: 32, pattern: "^[a-zA-Z][a-zA-Z0-9_]*$" }
    },
    ratio: {
        type: "double",
        default: 0.5,
        description: "Ratio (0.0-1.0)",
        constraints: { min: 0.0, max: 1.0 }
    },
    tags: {
        type: "array",
        default: [],
        description: "Tags (max 10 items)",
        items: { type: "string" },
        constraints: { maxItems: 10 }
    }
});

const cfg = getConfig();

console.log("[M28] === 02_constraints ===");
console.log("[M28] port:", cfg.port, "(range: 1-65535)");
console.log("[M28] name:", cfg.name, "(pattern: ^[a-zA-Z][a-zA-Z0-9_]*$, maxLength: 32)");
console.log("[M28] ratio:", cfg.ratio, "(range: 0.0-1.0)");
console.log("[M28] tags:", JSON.stringify(cfg.tags), "(maxItems: 10)");
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

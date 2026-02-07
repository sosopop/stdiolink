import { defineConfig, getConfig } from "stdiolink";

// M28 场景5：配置文件 + CLI 合并优先级
// 演示 --config-file=path 加载和 CLI > file > default 的优先级

defineConfig({
    port: {
        type: "int",
        required: true,
        description: "Listen port",
        constraints: { min: 1, max: 65535 }
    },
    name: {
        type: "string",
        required: true,
        description: "Application name"
    },
    debug: {
        type: "bool",
        default: false,
        description: "Enable debug mode"
    },
    mode: {
        type: "string",
        default: "normal",
        description: "Run mode",
        constraints: { enumValues: ["fast", "normal", "slow"] }
    },
    server: {
        type: "object",
        description: "Server configuration",
        fields: {
            host: { type: "string", default: "0.0.0.0", description: "Bind address" },
            port: { type: "int", default: 8080, description: "Server port" },
            ssl:  { type: "bool", default: false, description: "Enable SSL" }
        }
    },
    tags: {
        type: "array",
        default: [],
        description: "Tags",
        items: { type: "string" }
    },
    ratio: {
        type: "double",
        default: 0.5,
        description: "Processing ratio",
        constraints: { min: 0, max: 1 }
    }
});

const cfg = getConfig();

console.log("[M28] === 05_config_file_merge ===");
console.log("[M28] Priority: CLI > config-file > default");
console.log("[M28] port:", cfg.port);
console.log("[M28] name:", cfg.name);
console.log("[M28] debug:", cfg.debug);
console.log("[M28] mode:", cfg.mode);
console.log("[M28] server:", JSON.stringify(cfg.server));
console.log("[M28] tags:", JSON.stringify(cfg.tags));
console.log("[M28] ratio:", cfg.ratio);
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

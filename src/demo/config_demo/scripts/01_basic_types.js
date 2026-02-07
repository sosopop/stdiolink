import { defineConfig, getConfig } from "stdiolink";

// M28 场景1：基础类型 + 默认值填充
// 演示 string, int, double, bool, any 五种基础字段类型

defineConfig({
    name: {
        type: "string",
        required: true,
        description: "Application name"
    },
    port: {
        type: "int",
        required: true,
        description: "Listen port"
    },
    ratio: {
        type: "double",
        default: 0.5,
        description: "Processing ratio (0~1)"
    },
    debug: {
        type: "bool",
        default: false,
        description: "Enable debug mode"
    },
    metadata: {
        type: "any",
        default: null,
        description: "Arbitrary metadata"
    }
});

const cfg = getConfig();

console.log("[M28] === 01_basic_types ===");
console.log("[M28] name:", cfg.name, "(type:", typeof cfg.name + ")");
console.log("[M28] port:", cfg.port, "(type:", typeof cfg.port + ")");
console.log("[M28] ratio:", cfg.ratio, "(type:", typeof cfg.ratio + ")");
console.log("[M28] debug:", cfg.debug, "(type:", typeof cfg.debug + ")");
console.log("[M28] metadata:", cfg.metadata, "(type:", typeof cfg.metadata + ")");
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

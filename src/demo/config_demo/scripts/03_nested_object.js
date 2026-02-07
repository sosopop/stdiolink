import { defineConfig, getConfig } from "stdiolink";

// M28 场景3：嵌套对象配置
// 演示 object 类型嵌套字段和 CLI 嵌套路径覆盖 (--config.server.port=8080)

defineConfig({
    server: {
        type: "object",
        description: "Server configuration",
        fields: {
            host: { type: "string", default: "localhost", description: "Bind address" },
            port: { type: "int", required: true, description: "Listen port" },
            ssl:  { type: "bool", default: false, description: "Enable SSL" }
        }
    },
    database: {
        type: "object",
        description: "Database configuration",
        fields: {
            host: { type: "string", default: "127.0.0.1", description: "DB host" },
            port: { type: "int", default: 5432, description: "DB port" },
            name: { type: "string", required: true, description: "DB name" }
        }
    }
});

const cfg = getConfig();

console.log("[M28] === 03_nested_object ===");
console.log("[M28] server.host:", cfg.server.host);
console.log("[M28] server.port:", cfg.server.port);
console.log("[M28] server.ssl:", cfg.server.ssl);
console.log("[M28] database.host:", cfg.database.host);
console.log("[M28] database.port:", cfg.database.port);
console.log("[M28] database.name:", cfg.database.name);
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

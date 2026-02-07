import { getConfig } from "stdiolink";

// M28 场景3：嵌套对象配置

const cfg = getConfig();

console.log("[M28] === 03_nested_object ===");
console.log("[M28] server.host:", cfg.server.host);
console.log("[M28] server.port:", cfg.server.port);
console.log("[M28] server.ssl:", cfg.server.ssl);
console.log("[M28] database.host:", cfg.database.host);
console.log("[M28] database.port:", cfg.database.port);
console.log("[M28] database.name:", cfg.database.name);
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

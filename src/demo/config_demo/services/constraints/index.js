import { getConfig } from "stdiolink";

// M28 场景2：约束校验

const cfg = getConfig();

console.log("[M28] === 02_constraints ===");
console.log("[M28] port:", cfg.port, "(range: 1-65535)");
console.log("[M28] name:", cfg.name, "(pattern: ^[a-zA-Z][a-zA-Z0-9_]*$, maxLength: 32)");
console.log("[M28] ratio:", cfg.ratio, "(range: 0.0-1.0)");
console.log("[M28] tags:", JSON.stringify(cfg.tags), "(maxItems: 10)");
console.log("[M28] full config:", JSON.stringify(cfg, null, 2));

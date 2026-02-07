import { getConfig } from "stdiolink";

// M28 场景5：配置文件 + CLI 合并优先级
// 演示 --config-file=path 加载和 CLI > file > default 的优先级

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

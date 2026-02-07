import { defineConfig, getConfig } from "stdiolink";

// M28 场景6：只读对象 + 错误处理
// 演示 getConfig() 返回冻结对象、重复 defineConfig() 报错等边界行为

defineConfig({
    value: { type: "int", default: 42, description: "Test value" }
});

const cfg = getConfig();
console.log("[M28] === 06_readonly_and_errors ===");
console.log("[M28] value:", cfg.value);

// 测试1：尝试修改冻结对象
try {
    cfg.value = 999;
    console.log("[M28] FAIL: assignment should have thrown");
} catch (e) {
    console.log("[M28] PASS: assignment blocked:", e.message || String(e));
}

// 测试2：尝试删除属性
try {
    delete cfg.value;
    console.log("[M28] FAIL: delete should have thrown");
} catch (e) {
    console.log("[M28] PASS: delete blocked:", e.message || String(e));
}

// 测试3：重复调用 defineConfig
try {
    defineConfig({ other: { type: "int", default: 0 } });
    console.log("[M28] FAIL: duplicate defineConfig should have thrown");
} catch (e) {
    console.log("[M28] PASS: duplicate defineConfig blocked:", e.message || String(e));
}

// 测试4：多次 getConfig 返回相同值
const cfg2 = getConfig();
console.log("[M28] getConfig() consistency:", cfg.value === cfg2.value ? "PASS" : "FAIL");

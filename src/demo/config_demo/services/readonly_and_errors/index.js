import { getConfig } from "stdiolink";

// M28 场景6：只读对象 + 错误处理
// 演示 getConfig() 返回冻结对象，不可修改/删除

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

// 测试3：多次 getConfig 返回相同值
const cfg2 = getConfig();
console.log("[M28] getConfig() consistency:", cfg.value === cfg2.value ? "PASS" : "FAIL");

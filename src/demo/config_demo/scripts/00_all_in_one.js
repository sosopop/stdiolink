import { exec } from "stdiolink";

// M28 入口脚本：依次运行所有配置演示场景
// 由于 defineConfig() 只能调用一次，每个场景必须作为独立子进程运行

const scenarios = [
    {
        script: "01_basic_types.js",
        args: ["--config.name=myApp", "--config.port=8080"]
    },
    {
        script: "02_constraints.js",
        args: ["--config.port=8080", "--config.name=myService"]
    },
    {
        script: "03_nested_object.js",
        args: ["--config.server.port=8080", "--config.database.name=mydb"]
    },
    {
        script: "04_array_and_enum.js",
        args: ["--config.level=3", "--config.mode=fast"]
    },
    {
        script: "05_config_file_merge.js",
        args: ["--config-file=config_demo/config/sample_config.json", "--config.port=9999", "--config.name=cli-override"]
    },
    {
        script: "06_readonly_and_errors.js",
        args: []
    }
];

function findService() {
    const candidates = [
        "./stdiolink_service.exe",
        "./stdiolink_service"
    ];
    for (const c of candidates) {
        try {
            exec(c, ["--help"], { timeout: 3000 });
            return c;
        } catch (e) {
            // try next
        }
    }
    throw new Error("Cannot find stdiolink_service executable");
}

console.log("=== M28 config_demo: all scenarios ===");

const service = findService();
let passed = 0;
let failed = 0;

for (const s of scenarios) {
    console.log("");
    console.log("--- Running:", s.script, "---");
    try {
        const scriptPath = "config_demo/" + s.script;
        const result = exec(service, [scriptPath, ...s.args], { timeout: 10000 });
        if (result.stderr) {
            console.log(result.stderr.trim());
        }
        if (result.exitCode !== 0) {
            console.log("EXIT CODE:", result.exitCode);
            failed++;
        } else {
            passed++;
        }
    } catch (e) {
        console.log("ERROR:", e.message || String(e));
        failed++;
    }
}

console.log("");
console.log("=== M28 config_demo done:", passed, "passed,", failed, "failed ===");

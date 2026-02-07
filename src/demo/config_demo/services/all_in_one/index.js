import { exec } from "stdiolink";

// M28 入口脚本：依次运行所有配置演示场景（服务目录版）

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

const service = findService();

const scenarios = [
    {
        dir: "config_demo/services/basic_types",
        args: ["--config.name=myApp", "--config.port=8080"]
    },
    {
        dir: "config_demo/services/constraints",
        args: ["--config.port=8080", "--config.name=myService"]
    },
    {
        dir: "config_demo/services/nested_object",
        args: ["--config.server.port=8080", "--config.database.name=mydb"]
    },
    {
        dir: "config_demo/services/array_and_enum",
        args: ["--config.level=3", "--config.mode=fast"]
    },
    {
        dir: "config_demo/services/config_file_merge",
        args: [
            "--config-file=config_demo/services/config/sample_config.json",
            "--config.port=9999",
            "--config.name=cli-override"
        ]
    },
    {
        dir: "config_demo/services/readonly_and_errors",
        args: []
    }
];

console.log("=== M28 config_demo: all scenarios ===");

let passed = 0;
let failed = 0;

for (const s of scenarios) {
    console.log("");
    console.log("--- Running:", s.dir, "---");
    try {
        const result = exec(service, [s.dir, ...s.args], { timeout: 10000 });
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

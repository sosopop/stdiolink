import { exec } from "stdiolink";

// M28 入口脚本：依次运行所有配置演示场景（服务目录版）

function findService() {
    const candidates = [
        "./stdiolink_service.exe",
        "./stdiolink_service",
        "./build_ninja/bin/stdiolink_service.exe",
        "./build_ninja/bin/stdiolink_service",
        "stdiolink_service.exe",
        "stdiolink_service"
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

function findScenarioBase(service) {
    const bases = [
        "config_demo/services",
        "src/demo/config_demo/services"
    ];
    for (const base of bases) {
        try {
            const probe = exec(service, [`${base}/basic_types`, "--help"], { timeout: 5000 });
            if (probe.exitCode === 0) {
                return base;
            }
        } catch (e) {
            // try next
        }
    }
    throw new Error("Cannot find config_demo services directory");
}

const service = findService();
const scenarioBase = findScenarioBase(service);

const scenarios = [
    {
        dir: `${scenarioBase}/basic_types`,
        args: ["--config.name=myApp", "--config.port=8080"]
    },
    {
        dir: `${scenarioBase}/constraints`,
        args: ["--config.port=8080", "--config.name=myService"]
    },
    {
        dir: `${scenarioBase}/nested_object`,
        args: ["--config.server.port=8080", "--config.database.name=mydb"]
    },
    {
        dir: `${scenarioBase}/array_and_enum`,
        args: ["--config.level=3", "--config.mode=fast"]
    },
    {
        dir: `${scenarioBase}/config_file_merge`,
        args: [
            `--config-file=${scenarioBase}/config/sample_config.json`,
            "--config.port=9999",
            "--config.name=cli-override"
        ]
    },
    {
        dir: `${scenarioBase}/readonly_and_errors`,
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

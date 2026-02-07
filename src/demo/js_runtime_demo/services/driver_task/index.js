import { Driver } from "stdiolink";
import { startDriverAuto } from "../../shared/lib/runtime_utils.js";

(async () => {
    const driver = new Driver();
    const program = startDriverAuto(driver, "calculator_driver", ["--profile=keepalive"]);
    console.log("[M23] started:", program);

    const meta = driver.queryMeta(5000);
    if (!meta) {
        throw new Error("queryMeta failed");
    }
    console.log("[M23] meta:", meta.info.name, "commands:", meta.commands.length);

    const batchTask = driver.request("batch", {
        operations: [
            { type: "add", a: 1, b: 2 },
            { type: "mul", a: 3, b: 4 },
            { type: "sub", a: 10, b: 5 }
        ]
    });

    while (true) {
        const msg = batchTask.waitNext(5000);
        if (!msg) {
            throw new Error("waitNext timeout");
        }
        if (msg.status === "event") {
            console.log("[M23] batch event:", JSON.stringify(msg.data));
            continue;
        }
        if (msg.status === "done") {
            console.log("[M23] batch done:", JSON.stringify(msg.data));
            break;
        }
        throw new Error(`batch failed: ${JSON.stringify(msg.data)}`);
    }

    const badTask = driver.request("divide", { a: 1, b: 0 });
    const badMsg = badTask.waitNext(5000);
    console.log("[M23] error sample:", JSON.stringify(badMsg));

    driver.terminate();
})();

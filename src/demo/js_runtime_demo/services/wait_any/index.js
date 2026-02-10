import { openDriver, waitAny } from "stdiolink";
import { openDriverAuto } from "../../shared/lib/runtime_utils.js";

(async () => {
    const a = await openDriverAuto(openDriver, "calculator_driver");
    const b = await openDriverAuto(openDriver, "calculator_driver");

    const taskA = a.$rawRequest("batch", {
        operations: [
            { type: "add", a: 1, b: 2 },
            { type: "mul", a: 3, b: 4 },
            { type: "sub", a: 9, b: 1 }
        ]
    });
    const taskB = b.$rawRequest("batch", {
        operations: [
            { type: "add", a: 10, b: 5 },
            { type: "div", a: 8, b: 2 }
        ]
    });

    let doneCount = 0;
    while (doneCount < 2) {
        const result = await waitAny([taskA, taskB], 5000);
        if (!result) {
            console.log("[M33] waitAny timeout/all-done");
            break;
        }

        const prefix = `[M33] task#${result.taskIndex}`;
        if (result.msg.status === "event") {
            console.log(prefix, "event:", JSON.stringify(result.msg.data));
            continue;
        }
        if (result.msg.status === "done") {
            doneCount += 1;
            console.log(prefix, "done:", JSON.stringify(result.msg.data));
            continue;
        }
        throw new Error(`${prefix} error: ${JSON.stringify(result.msg.data)}`);
    }

    const finished = a.$rawRequest("add", { a: 1, b: 1 });
    finished.waitNext(5000);
    const doneTaskResult = await waitAny([finished], 200);
    console.log("[M33] done task waitAny:", doneTaskResult);

    const emptyResult = await waitAny([]);
    console.log("[M33] empty waitAny:", emptyResult);

    a.$close();
    b.$close();
})();

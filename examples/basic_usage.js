import { Driver } from "stdiolink";

// Update the binary path for your environment.
const d = new Driver();
if (!d.start("./stdio.drv.calculator.exe", ["--profile=keepalive"])) {
    throw new Error("failed to start stdio.drv.calculator");
}

const task = d.request("add", { a: 10, b: 20 });
const msg = task.waitNext(5000);
if (!msg || msg.status !== "done") {
    throw new Error("unexpected task result");
}

console.log("result:", JSON.stringify(msg.data));
d.terminate();

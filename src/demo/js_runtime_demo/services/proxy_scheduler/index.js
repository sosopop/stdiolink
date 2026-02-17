import { openDriver } from "stdiolink";
import { openDriverAuto } from "../../shared/lib/runtime_utils.js";

(async () => {
    const a = await openDriverAuto(openDriver, "stdio.drv.calculator");
    const b = await openDriverAuto(openDriver, "stdio.drv.calculator");

    const [ra, rb] = await Promise.all([
        a.add({ a: 10, b: 20 }),
        b.add({ a: 7, b: 5 })
    ]);
    console.log("[M25] parallel across instances:", ra.result, rb.result);

    let busyCaught = false;
    const p1 = a.add({ a: 1, b: 2 });
    try {
        a.multiply({ a: 2, b: 3 });
    } catch (e) {
        busyCaught = String(e).includes("DriverBusyError");
    }
    const first = await p1;
    console.log("[M25] same-instance busy:", busyCaught, "first:", first.result);

    const rawTask = a.$rawRequest("subtract", { a: 9, b: 4 });
    const rawMsg = rawTask.waitNext(5000);
    console.log("[M25] raw request:", JSON.stringify(rawMsg));

    a.$close();
    b.$close();
})();

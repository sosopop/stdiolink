import { openDriver } from "stdiolink";

(async () => {
    // Update the binary path for your environment.
    const drvA = await openDriver("./stdio.drv.calculator.exe");
    const drvB = await openDriver("./stdio.drv.calculator.exe");

    const [a, b] = await Promise.all([
        drvA.add({ a: 10, b: 20 }),
        drvB.add({ a: 30, b: 40 })
    ]);

    console.log("A:", JSON.stringify(a), "B:", JSON.stringify(b));
    drvA.$close();
    drvB.$close();
})();

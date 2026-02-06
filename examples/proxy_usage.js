import { openDriver } from "stdiolink";

(async () => {
    // Update the binary path for your environment.
    const calc = await openDriver("./calculator_driver.exe");
    const sum = await calc.add({ a: 5, b: 3 });
    console.log("add result:", JSON.stringify(sum));
    console.log("driver:", calc.$meta.info.name);
    calc.$close();
})();

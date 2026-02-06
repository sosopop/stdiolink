import { run as runEngineModules } from "./scenarios/01_engine_modules.js";
import { run as runDriverTask } from "./scenarios/02_driver_task.js";
import { run as runProxyScheduler } from "./scenarios/03_proxy_scheduler.js";
import { run as runProcessAndTypes } from "./scenarios/04_process_and_types.js";

(async () => {
    console.log("=== JS runtime demo start (M21-M27) ===");
    await runEngineModules();
    await runDriverTask();
    await runProxyScheduler();
    await runProcessAndTypes();
    console.log("=== JS runtime demo done (M21-M27) ===");
})();

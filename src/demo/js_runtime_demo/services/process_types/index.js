import { exec } from "stdiolink";
import { resolveDriver } from "stdiolink/driver";
import { firstSuccess } from "../../shared/lib/runtime_utils.js";

function runEcho() {
    return firstSuccess([
        () => exec("cmd", ["/c", "echo", "hello-from-exec"]),
        () => exec("echo", ["hello-from-exec"])
    ]);
}

function exportTypeScriptDeclaration() {
    const program = resolveDriver("stdio.drv.calculator");
    return exec(program, ["--export-doc=ts"]);
}

(async () => {
    const echoResult = runEcho();
    console.log("[M24] exec exit:", echoResult.exitCode);
    console.log("[M24] exec stdout:", echoResult.stdout.trim());

    const tsResult = exportTypeScriptDeclaration();
    const lines = tsResult.stdout.split("\n");
    const preview = lines.slice(0, 12).join("\n");
    console.log("[M26] d.ts preview:");
    console.log(preview);
    console.log("[M26] has DriverProxy type:", tsResult.stdout.includes("export type DriverProxy"));
})();

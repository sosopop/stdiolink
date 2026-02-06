import { exec } from "stdiolink";

const r = exec("cmd", ["/c", "echo", "hello from exec"]);
console.log("exitCode:", r.exitCode);
console.log("stdout:", r.stdout);

import { resolveDriver } from "stdiolink/driver";

export function startDriverAuto(driver, baseName, args = []) {
    const program = resolveDriver(baseName);
    if (!driver.start(program, args)) throw new Error(`driver.start failed: ${program}`);
    return program;
}

export async function openDriverAuto(openDriver, baseName, args = []) {
    const program = resolveDriver(baseName);
    return await openDriver(program, args);
}

export function firstSuccess(runners) {
    let lastError = null;
    for (const run of runners) {
        try { return run(); } catch (e) { lastError = e; }
    }
    throw (lastError || new Error("all runners failed"));
}

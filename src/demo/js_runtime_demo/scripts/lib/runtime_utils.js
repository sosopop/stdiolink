export function driverPathCandidates(baseName) {
    return [
        `./${baseName}.exe`,
        `./${baseName}`,
        `./build_ninja/bin/${baseName}.exe`,
        `./build_ninja/bin/${baseName}`
    ];
}

export function startDriverAuto(driver, baseName, args = []) {
    const candidates = driverPathCandidates(baseName);
    for (const program of candidates) {
        if (driver.start(program, args)) {
            return program;
        }
    }
    throw new Error(`cannot start driver: ${baseName}`);
}

export async function openDriverAuto(openDriver, baseName, args = []) {
    const candidates = driverPathCandidates(baseName);
    let lastError = null;
    for (const program of candidates) {
        try {
            return await openDriver(program, args);
        } catch (e) {
            lastError = e;
        }
    }
    throw (lastError || new Error(`cannot open driver: ${baseName}`));
}

export function firstSuccess(runners) {
    let lastError = null;
    for (const run of runners) {
        try {
            return run();
        } catch (e) {
            lastError = e;
        }
    }
    throw (lastError || new Error("all runners failed"));
}

# JS Runtime Demo (M21-M27)

This demo set is under `src/demo/js_runtime_demo` and is copied to:

- `build_ninja/bin/js_runtime_demo`

It demonstrates all milestone features from M21 to M27:

- M21: JS engine scaffold and `console.*` bridge
- M22: ES module loading (relative + parent imports)
- M23: `Driver` / `Task` bindings
- M24: `exec(program, args?, options?)` process binding
- M25: `openDriver` proxy and scheduler behavior
- M26: Driver `--export-doc=ts` TypeScript declaration export
- M27: End-to-end composition

## Build

```powershell
build_ninja.bat
```

## Run one-by-one

Run from repo root:

```powershell
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/01_engine_modules.js
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/02_driver_task.js
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/03_proxy_scheduler.js
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/04_process_and_types.js
```

## Run all in one

```powershell
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/00_all_in_one.js
```

## Expected behavior summary

- Scenario 01 prints module math results and console messages.
- Scenario 02 starts calculator driver, queries meta, receives `batch` events, and prints one error response sample.
- Scenario 03 uses `openDriver`, shows parallel calls across two driver instances, and shows same-instance busy protection.
- Scenario 04 runs `exec` and exports TS declaration via `calculator_driver --export-doc=ts`.

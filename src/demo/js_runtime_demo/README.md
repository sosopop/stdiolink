# JS Runtime Demo (M21-M27)

This demo set is under `src/demo/js_runtime_demo` and is copied to:

- `build/bin/js_runtime_demo`

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
build.bat
```

## Run one-by-one

Run from `build/bin`:

```powershell
cd build/bin

./stdiolink_service.exe js_runtime_demo/services/engine_modules
./stdiolink_service.exe js_runtime_demo/services/driver_task
./stdiolink_service.exe js_runtime_demo/services/proxy_scheduler
./stdiolink_service.exe js_runtime_demo/services/process_types
```

## Run all in one

```powershell
cd build/bin
./stdiolink_service.exe js_runtime_demo/services/basic_demo
```

## Expected behavior summary

- engine_modules prints module math results and console messages.
- driver_task starts calculator driver, queries meta, receives `batch` events, and prints one error response sample.
- proxy_scheduler uses `openDriver`, shows parallel calls across two driver instances, and shows same-instance busy protection.
- process_types runs `exec` and exports TS declaration via `calculator_driver --export-doc=ts`.

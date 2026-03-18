#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import platform
import re
import shutil
import socket
import stat
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence


PORTS_TO_CHECK = (6200, 3000)
TEST_SUITES = ("gtest", "vitest", "playwright", "smoke")
WINDOWS_RELEASE_SCRIPTS = ("start.bat", "start.ps1", "dev.bat", "dev.ps1")
UNIX_RELEASE_SCRIPTS = ("start.sh", "dev.sh", "install_service.sh", "uninstall_service.sh")
DEFAULT_INSTALL_DIR = "install"
DEFAULT_WINDOWS_TRIPLET = "x64-windows"


class ReleaseError(RuntimeError):
    pass


def log(message: str) -> None:
    print(message, flush=True)


def warn(message: str) -> None:
    print(f"WARNING: {message}", file=sys.stderr, flush=True)


def is_windows() -> bool:
    return os.name == "nt"


def root_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def tools_dir() -> Path:
    return Path(__file__).resolve().parent


def resolve_path(path: str, base: Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate.resolve()
    return (base / candidate).resolve()


def normalize_config(config: str) -> str:
    value = config.strip().lower()
    if value not in {"debug", "release"}:
        raise ReleaseError(f"Invalid --config value: {config} (expected: debug or release)")
    return value


def command_path(name: str) -> str | None:
    if is_windows():
        windows_name = f"{name}.cmd"
        found = shutil.which(windows_name)
        if found:
            return found
    return shutil.which(name)


def run_command(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    check: bool = True,
    failure_message: str | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(list(command), cwd=str(cwd) if cwd else None, env=env, text=True)
    if check and result.returncode != 0:
        if failure_message:
            raise ReleaseError(f"{failure_message} (exit code {result.returncode})")
        raise ReleaseError(f"Command failed: {' '.join(command)} (exit code {result.returncode})")
    return result


def capture_text_command(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )


def can_bind_port(port: int) -> bool:
    addresses = [("127.0.0.1", socket.AF_INET)]
    if socket.has_ipv6:
        addresses.append(("::1", socket.AF_INET6))

    for host, family in addresses:
        sock = socket.socket(family, socket.SOCK_STREAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if family == socket.AF_INET6:
                sock.bind((host, port, 0, 0))
            else:
                sock.bind((host, port))
        except OSError as exc:
            if family == socket.AF_INET6 and getattr(exc, "winerror", None) in {10047, 10049}:
                continue
            if family == socket.AF_INET6 and exc.errno in {47, 49, 97, 99}:
                continue
            return False
        finally:
            sock.close()
    return True


def powershell_executable() -> str | None:
    return shutil.which("pwsh") or shutil.which("powershell")


def windows_port_listeners(port: int) -> list[dict[str, str]]:
    powershell = powershell_executable()
    if not powershell:
        return []

    script = f"""
$rows = Get-NetTCPConnection -ErrorAction SilentlyContinue | Where-Object {{ $_.LocalPort -eq {port} }}
if (-not $rows) {{
    exit 0
}}
$result = foreach ($row in $rows) {{
    $proc = Get-Process -Id $row.OwningProcess -ErrorAction SilentlyContinue
    [pscustomobject]@{{
        localAddress = $row.LocalAddress
        localPort = $row.LocalPort
        state = $row.State
        pid = $row.OwningProcess
        processName = if ($proc) {{ $proc.ProcessName }} else {{ "" }}
    }}
}}
$result | ConvertTo-Json -Compress
"""
    result = subprocess.run(
        [powershell, "-NoProfile", "-Command", script],
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        return []

    payload = (result.stdout or "").strip()
    if not payload:
        return []

    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError:
        return []

    if isinstance(parsed, dict):
        return [parsed]
    if isinstance(parsed, list):
        return [item for item in parsed if isinstance(item, dict)]
    return []


def windows_excluded_port_ranges() -> list[tuple[str, int, int]]:
    ranges: list[tuple[str, int, int]] = []
    for family, command in (
        ("ipv4", ["netsh", "interface", "ipv4", "show", "excludedportrange", "protocol=tcp"]),
        ("ipv6", ["netsh", "interface", "ipv6", "show", "excludedportrange", "protocol=tcp"]),
    ):
        result = capture_text_command(command)
        if result.returncode != 0:
            continue
        for line in (result.stdout or "").splitlines():
            match = re.match(r"\s*(\d+)\s+(\d+)", line)
            if match:
                ranges.append((family, int(match.group(1)), int(match.group(2))))
    return ranges


def unix_port_listeners(port: int) -> list[str]:
    if shutil.which("lsof"):
        result = capture_text_command(["lsof", "-nP", f"-iTCP:{port}", "-sTCP:LISTEN"])
        if result.returncode == 0:
            listeners: list[str] = []
            for line in (result.stdout or "").splitlines()[1:]:
                parts = line.split()
                if len(parts) >= 2:
                    command_name = parts[0]
                    pid = parts[1]
                    listeners.append(f"PID {pid}: {command_name}")
            if listeners:
                return listeners

    if shutil.which("ss"):
        result = capture_text_command(["ss", "-ltnp", f"sport = :{port}"])
        if result.returncode == 0:
            listeners = []
            for line in (result.stdout or "").splitlines():
                if "LISTEN" not in line:
                    continue
                match = re.search(r'users:\(\("([^"]+)",pid=(\d+)', line)
                if match:
                    listeners.append(f"PID {match.group(2)}: {match.group(1)}")
            if listeners:
                return listeners

    return []


def describe_port_conflict(port: int) -> str:
    if is_windows():
        listeners = windows_port_listeners(port)
        if listeners:
            details = []
            for entry in listeners:
                pid = str(entry.get("pid", "")).strip() or "unknown"
                process_name = str(entry.get("processName", "")).strip() or "unknown"
                local_address = str(entry.get("localAddress", "")).strip() or "unknown"
                state = str(entry.get("state", "")).strip() or "unknown"
                details.append(f"PID {pid}: {process_name} ({local_address}:{port}, {state})")
            return "; ".join(details)

        reserved = []
        for family, start, end in windows_excluded_port_ranges():
            if start <= port <= end:
                reserved.append(f"{family} excluded range {start}-{end}")
        if reserved:
            return "no owning process identified; matched " + ", ".join(reserved)

        return "no owning process identified; the port may be reserved by Windows or held by a privileged service"

    listeners = unix_port_listeners(port)
    if listeners:
        return "; ".join(listeners)
    return "no owning process identified"


def assert_required_ports_available() -> None:
    conflicts = [port for port in PORTS_TO_CHECK if not can_bind_port(port)]
    if conflicts:
        details = [f"Port {port}: {describe_port_conflict(port)}" for port in conflicts]
        raise ReleaseError(
            f"Required port(s) are unavailable for binding: {', '.join(str(port) for port in conflicts)}. "
            "Ports 6200 and 3000 must be available before running this command.\n  "
            + "\n  ".join(details)
        )


def blocking_processes_windows() -> list[tuple[str, str]]:
    result = capture_text_command(["tasklist", "/FO", "CSV", "/NH"])
    if result.returncode != 0:
        raise ReleaseError("Failed to inspect running processes via tasklist")

    blocking: list[tuple[str, str]] = []
    for row in csv.reader(line for line in (result.stdout or "").splitlines() if line.strip()):
        if len(row) < 2:
            continue
        image_name = row[0].strip()
        pid = row[1].strip()
        lowered = image_name.lower()
        if lowered in {"stdiolink_server.exe", "stdiolink_service.exe"} or lowered.startswith("stdio.drv"):
            blocking.append((pid, image_name))
    return blocking


def blocking_processes_unix() -> list[tuple[str, str]]:
    result = capture_text_command(["ps", "-eo", "pid=,comm=,args="])
    if result.returncode != 0:
        raise ReleaseError("Failed to inspect running processes via ps")

    blocking: list[tuple[str, str]] = []
    for line in (result.stdout or "").splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) < 2:
            continue
        pid = parts[0]
        command_name = parts[1].lower()
        args = parts[2].lower() if len(parts) > 2 else command_name
        if (
            command_name in {"stdiolink_server", "stdiolink_server.exe", "stdiolink_service", "stdiolink_service.exe"}
            or command_name.startswith("stdio.drv")
            or re.search(r"(^|[ /\\])stdiolink_server(\.exe)?([ ]|$)", args)
            or re.search(r"(^|[ /\\])stdiolink_service(\.exe)?([ ]|$)", args)
            or re.search(r"(^|[ /\\])stdio\.drv[^ /\\]*(\.exe)?([ ]|$)", args)
        ):
            blocking.append((pid, parts[1]))
    return blocking


def assert_no_blocking_processes() -> None:
    blocking = blocking_processes_windows() if is_windows() else blocking_processes_unix()
    if blocking:
        lines = [f"PID {pid}: {name}" for pid, name in blocking]
        raise ReleaseError(
            "Blocking runtime processes are still running. Stop them before running this command:\n  "
            + "\n  ".join(lines)
        )


def git_rev(*rev_args: str) -> str:
    result = capture_text_command(["git", "-C", str(root_dir()), "rev-parse", *rev_args])
    value = (result.stdout or "").strip()
    if result.returncode == 0 and value:
        return value
    return "unknown"


def vcpkg_executable_name() -> str:
    return "vcpkg.exe" if is_windows() else "vcpkg"


def detect_vcpkg(root: Path) -> tuple[Path, Path]:
    executable = vcpkg_executable_name()
    checks: list[tuple[Path, str]] = []

    env_root = os.environ.get("VCPKG_ROOT", "").strip()
    if env_root:
        checks.append((Path(env_root).expanduser(), f"Found vcpkg via VCPKG_ROOT: {env_root}"))

    checks.append((root / "vcpkg", f"Found vcpkg in current directory: {root / 'vcpkg'}"))
    checks.append((root.parent / "vcpkg", f"Found vcpkg in parent directory: {root.parent / 'vcpkg'}"))

    for candidate_root, message in checks:
        if (candidate_root / executable).is_file():
            log(message)
            toolchain = candidate_root / "scripts" / "buildsystems" / "vcpkg.cmake"
            if not toolchain.is_file():
                raise ReleaseError(f"vcpkg toolchain file not found at {toolchain}")
            return candidate_root.resolve(), toolchain.resolve()

    vcpkg_path = shutil.which("vcpkg")
    if vcpkg_path:
        candidate_root = Path(vcpkg_path).resolve().parent
        log(f"Found vcpkg in PATH: {candidate_root}")
        toolchain = candidate_root / "scripts" / "buildsystems" / "vcpkg.cmake"
        if not toolchain.is_file():
            raise ReleaseError(f"vcpkg toolchain file not found at {toolchain}")
        return candidate_root, toolchain.resolve()

    searched = [
        "VCPKG_ROOT environment variable",
        f"Current directory: {root / 'vcpkg'}",
        f"Parent directory: {root.parent / 'vcpkg'}",
        "System PATH",
    ]
    raise ReleaseError(
        "vcpkg not found!\nSearched locations:\n  - " + "\n  - ".join(searched) + "\nPlease install vcpkg or ensure it's in one of the above locations."
    )


def detect_unix_triplet() -> str:
    os_name = platform.system()
    arch = platform.machine().lower()

    if os_name == "Darwin":
        triplet = "arm64-osx" if arch in {"arm64", "aarch64"} else "x64-osx"
    elif os_name == "Linux":
        triplet = "arm64-linux" if arch in {"arm64", "aarch64"} else "x64-linux"
    else:
        raise ReleaseError(f"Unsupported OS '{os_name}'. Please set VCPKG_TARGET_TRIPLET manually.")

    log(f"Detected OS: {os_name}, Arch: {arch}, vcpkg triplet: {triplet}")
    return triplet


def build_runtime(*, build_dir_arg: str, config: str, clean_runtime: bool) -> Path:
    root = root_dir()
    build_dir_abs = resolve_path(build_dir_arg, root)
    runtime_dir = build_dir_abs / f"runtime_{config}"
    if clean_runtime and runtime_dir.exists():
        shutil.rmtree(runtime_dir)

    build_type = "Debug" if config == "debug" else "Release"
    log(f"=== Building latest binaries ({build_type}) ===")
    log(f"Build Directory: {build_dir_arg}")
    log(f"Install Directory: {DEFAULT_INSTALL_DIR}")

    _, toolchain = detect_vcpkg(root)
    log(f"Using vcpkg toolchain: {toolchain}")

    if not build_dir_abs.is_dir():
        log("Creating build directory...")
        build_dir_abs.mkdir(parents=True, exist_ok=True)

    install_dir = (root / DEFAULT_INSTALL_DIR).resolve()
    vcpkg_installed_dir = (root / "vcpkg_installed").resolve()

    cmake_configure = [
        "cmake",
        "-S",
        str(root),
        "-B",
        str(build_dir_abs),
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        f"-DVCPKG_INSTALLED_DIR={vcpkg_installed_dir}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-G",
        "Ninja",
    ]

    if is_windows():
        cmake_configure.append(f"-DVCPKG_TARGET_TRIPLET={DEFAULT_WINDOWS_TRIPLET}")
    else:
        cmake_configure.append(f"-DVCPKG_TARGET_TRIPLET={detect_unix_triplet()}")
        overlay_ports_dir = root / "vcpkg-overlay-ports"
        if overlay_ports_dir.is_dir():
            log(f"Using vcpkg overlay ports: {overlay_ports_dir}")
            cmake_configure.append(f"-DVCPKG_OVERLAY_PORTS={overlay_ports_dir.resolve()}")

    log("========================================")
    log("Configuring project with CMake...")
    log("========================================")
    run_command(cmake_configure, cwd=root, failure_message="CMake configuration failed")

    log("========================================")
    log("Building project...")
    log("========================================")
    run_command(
        ["cmake", "--build", str(build_dir_abs), "--config", build_type, "--parallel", "8"],
        cwd=root,
        failure_message="Build failed",
    )

    log("========================================")
    log("Installing project...")
    log("========================================")
    run_command(
        ["cmake", "--install", str(build_dir_abs), "--config", build_type],
        cwd=root,
        failure_message="Installation failed",
    )

    log("========================================")
    log("Build completed successfully!")
    log(f"Runtime directory: {build_dir_arg}/runtime_{config}")
    log("========================================")
    return runtime_dir


def ensure_runtime_exists(build_dir_arg: str, config: str) -> Path:
    runtime_dir = resolve_path(build_dir_arg, root_dir()) / f"runtime_{config}"
    if not runtime_dir.is_dir():
        hint = "build.bat" if is_windows() else "./build.sh"
        raise ReleaseError(f"Runtime directory not found: {runtime_dir}\nPlease build project first, for example: {hint}")
    return runtime_dir


def executable_name(stem: str) -> str:
    return f"{stem}.exe" if is_windows() else stem


def raw_bin_dir(build_dir_arg: str, config: str) -> Path:
    return resolve_path(build_dir_arg, root_dir()) / config


def runtime_bin_dir(build_dir_arg: str, config: str) -> Path:
    return ensure_runtime_exists(build_dir_arg, config) / "bin"


def selected_suites(args: argparse.Namespace) -> list[str]:
    requested = [suite for suite in TEST_SUITES if getattr(args, suite)]
    return requested or list(TEST_SUITES)


def run_gtest(build_dir_arg: str, config: str) -> None:
    log("=== GTest (C++) ===")
    binary = runtime_bin_dir(build_dir_arg, config) / executable_name("stdiolink_tests")
    if not binary.is_file():
        raise ReleaseError(f"GTest binary not found at {binary}")
    run_command([str(binary)], failure_message="GTest failed")
    log("  GTest passed.")


def run_vitest() -> None:
    log("=== Vitest (WebUI unit tests) ===")
    webui_dir = root_dir() / "src" / "webui"
    if not (webui_dir / "node_modules").is_dir():
        raise ReleaseError(f"node_modules not found in {webui_dir}")
    npm = command_path("npm")
    if not npm:
        raise ReleaseError("npm not found")
    run_command([npm, "run", "test"], cwd=webui_dir, failure_message="Vitest failed")
    log("  Vitest passed.")


def run_playwright() -> None:
    log("=== Playwright (E2E tests) ===")
    webui_dir = root_dir() / "src" / "webui"
    if not (webui_dir / "node_modules").is_dir():
        raise ReleaseError(f"node_modules not found in {webui_dir}")
    npx = command_path("npx")
    if not npx:
        raise ReleaseError("npx not found")
    run_command([npx, "playwright", "install", "chromium"], cwd=webui_dir, failure_message="Playwright browser install failed")
    run_command([npx, "playwright", "test"], cwd=webui_dir, failure_message="Playwright tests failed")
    log("  Playwright passed.")


def run_smoke(build_dir_arg: str, config: str) -> None:
    log("=== Smoke (Python) ===")
    smoke_script = root_dir() / "src" / "smoke_tests" / "run_smoke.py"
    if not smoke_script.is_file():
        raise ReleaseError(f"Smoke runner not found at {smoke_script}")
    python_exe = sys.executable
    if not python_exe:
        raise ReleaseError("python interpreter not found")
    env = os.environ.copy()
    runtime_dir = ensure_runtime_exists(build_dir_arg, config)
    env["STDIOLINK_BIN_DIR"] = str(runtime_dir / "bin")
    env["STDIOLINK_DATA_ROOT"] = str(runtime_dir / "data_root")
    run_command([python_exe, str(smoke_script), "--plan", "all"], env=env, failure_message="Smoke tests failed")
    log("  Smoke passed.")


def run_selected_tests(build_dir_arg: str, config: str, suites: Sequence[str]) -> None:
    for suite in suites:
        if suite == "gtest":
            run_gtest(build_dir_arg, config)
        elif suite == "vitest":
            run_vitest()
        elif suite == "playwright":
            run_playwright()
        elif suite == "smoke":
            run_smoke(build_dir_arg, config)
    log("")
    log(f"=== All {len(suites)} test suite(s) passed ===")


def should_skip_binary(name: str, with_tests: bool) -> bool:
    base = Path(name).stem
    lowered = name.lower()
    if base in {"bin_scan_orchestrator_service_tests", "exec_runner_service_tests"}:
        return True
    if not with_tests and (base == "stdiolink_tests" or base.startswith("test_") or base == "gtest"):
        return True
    if base in {"demo_host", "driverlab"}:
        return True
    if lowered.endswith((".log", ".tmp", ".json")):
        return True
    return False


def copy_release_script_files(destination_dir: Path) -> None:
    template_dir = tools_dir() / ("release_scripts" if is_windows() else "release_scripts_unix")
    names = WINDOWS_RELEASE_SCRIPTS if is_windows() else UNIX_RELEASE_SCRIPTS
    for name in names:
        source = template_dir / name
        if not source.is_file():
            raise ReleaseError(f"Release launcher template not found: {source}")
        target = destination_dir / name
        shutil.copy2(source, target)
        if not is_windows():
            target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def build_webui() -> Path:
    webui_dir = root_dir() / "src" / "webui"
    package_json = webui_dir / "package.json"
    if not package_json.is_file():
        raise ReleaseError("WebUI source not found")
    npm = command_path("npm")
    if not npm:
        raise ReleaseError("npm not found. Install Node.js or use --skip-webui to skip WebUI build.")

    log("Building WebUI...")
    result = run_command([npm, "ci", "--ignore-scripts"], cwd=webui_dir, check=False)
    if result.returncode != 0:
        log("  npm ci failed, retrying with npm install ...")
        run_command([npm, "install", "--ignore-scripts"], cwd=webui_dir, failure_message="npm install failed")
    run_command([npm, "run", "build"], cwd=webui_dir, failure_message="WebUI build failed")
    dist_dir = webui_dir / "dist"
    if not dist_dir.is_dir():
        raise ReleaseError("WebUI build succeeded but dist/ is missing")
    return dist_dir


def copytree_contents(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for child in source.iterdir():
        target = destination / child.name
        if child.is_dir():
            shutil.copytree(child, target, dirs_exist_ok=True)
        else:
            shutil.copy2(child, target)


def copy_runtime_binaries(package_dir: Path, runtime_dir: Path, with_tests: bool) -> None:
    log("Copying binaries from runtime...")
    runtime_bin = runtime_dir / "bin"
    package_bin = package_dir / "bin"
    for file in runtime_bin.iterdir():
        if file.is_file() and not should_skip_binary(file.name, with_tests):
            shutil.copy2(file, package_bin / file.name)

    log("Copying Qt plugin directories...")
    for directory in sorted(child for child in runtime_bin.iterdir() if child.is_dir()):
        target_dir = package_bin / directory.name
        target_dir.mkdir(parents=True, exist_ok=True)
        copied_any = False
        for library in sorted(child for child in directory.iterdir() if child.is_file()):
            if is_windows() and library.suffix.lower() == ".dll":
                stem = library.stem
                if stem.endswith("d") and (directory / f"{stem[:-1]}.dll").is_file():
                    continue
                shutil.copy2(library, target_dir / library.name)
                copied_any = True
            elif not is_windows() and (
                library.name.endswith(".so")
                or ".so." in library.name
                or library.name.endswith(".dylib")
                or library.name.endswith(".dll")
            ):
                shutil.copy2(library, target_dir / library.name)
                copied_any = True
        if copied_any:
            log(f"  + {directory.name}/")


def write_default_config(package_dir: Path) -> None:
    config_path = package_dir / "data_root" / "config.json"
    if config_path.is_file():
        return
    log("Generating default config.json...")
    config_path.write_text(
        json.dumps({"host": "127.0.0.1", "port": 6200, "logLevel": "info"}, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
    )


def write_manifest(package_dir: Path, package_name: str, build_dir_abs: Path, with_tests: bool, skip_webui: bool, skip_tests: bool) -> Path:
    manifest_path = package_dir / "RELEASE_MANIFEST.txt"
    lines = [
        f"package_name={package_name}",
        f"created_at={dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %z')}",
        f"git_commit={git_rev('HEAD')}",
        f"build_dir={build_dir_abs}",
        f"with_tests={int(with_tests)}",
        f"skip_webui={int(skip_webui)}",
        f"skip_tests={int(skip_tests)}",
        "",
        "[bin]",
    ]

    bin_dir = package_dir / "bin"
    lines.extend(file.name for file in sorted(bin_dir.iterdir()) if file.is_file())
    lines.append("")
    lines.append("[webui]")
    webui_dir = package_dir / "data_root" / "webui"
    if (webui_dir / "index.html").is_file():
        lines.append("status=bundled")
        lines.extend(file.name for file in sorted(webui_dir.rglob("*")) if file.is_file())
    else:
        lines.append("status=not_included")
    manifest_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return manifest_path


def run_duplicate_check(package_dir: Path) -> None:
    log("Checking for duplicate components...")
    errors: list[str] = []

    bin_dir = package_dir / "bin"
    if bin_dir.is_dir():
        for file in sorted(child for child in bin_dir.iterdir() if child.is_file() and child.name.startswith("stdio.drv.")):
            errors.append(f"ERROR: driver in bin/: {file.name} (should be in data_root/drivers/ only)")

    drivers_dir = package_dir / "data_root" / "drivers"
    if drivers_dir.is_dir():
        grouped: dict[str, list[str]] = {}
        for file in sorted(child for child in drivers_dir.rglob("*") if child.is_file() and child.name.startswith("stdio.drv.")):
            grouped.setdefault(file.name, []).append(file.parent.name)
        for name in sorted(grouped):
            locations = grouped[name]
            if len(locations) > 1:
                errors.append(f"ERROR: duplicate driver '{name}' in: {', '.join(locations)}")

    if errors:
        for message in errors:
            log(message)
        raise ReleaseError(f"{len(errors)} duplicate(s) found")

    log("No duplicates found.")


def package_release(args: argparse.Namespace) -> None:
    assert_required_ports_available()
    assert_no_blocking_processes()

    config = "release"
    if args.skip_tests and any(getattr(args, suite) for suite in TEST_SUITES):
        raise ReleaseError("--skip-tests cannot be combined with suite selection flags")

    runtime_dir = build_runtime(build_dir_arg=args.build_dir, config=config, clean_runtime=True) if not args.skip_build else ensure_runtime_exists(args.build_dir, config)
    if not runtime_dir.is_dir():
        raise ReleaseError(f"Runtime directory not found: {runtime_dir}")

    dist_dir: Path | None = None
    if not args.skip_webui:
        dist_dir = build_webui()

    suites = selected_suites(args)
    if not args.skip_tests:
        log("=== Running test suites ===")
        run_selected_tests(args.build_dir, config, suites)

    package_name = args.name or f"stdiolink_{dt.datetime.now().strftime('%Y%m%d_%H%M%S')}_{git_rev('--short', 'HEAD')}"
    build_dir_abs = resolve_path(args.build_dir, root_dir())
    output_dir_abs = resolve_path(args.output_dir, root_dir())
    package_dir = output_dir_abs / package_name

    log("Preparing release package:")
    log(f"  root        : {root_dir()}")
    log(f"  runtime     : {runtime_dir}")
    log(f"  output root : {output_dir_abs}")
    log(f"  package dir : {package_dir}")
    log(f"  with tests  : {int(args.with_tests)}")
    log(f"  skip webui  : {int(args.skip_webui)}")
    log(f"  skip tests  : {int(args.skip_tests)}")

    if package_dir.exists():
        shutil.rmtree(package_dir)
    for rel_dir in (
        "bin",
        "data_root/drivers",
        "data_root/services",
        "data_root/projects",
        "data_root/workspaces",
        "data_root/logs",
    ):
        (package_dir / rel_dir).mkdir(parents=True, exist_ok=True)

    copy_runtime_binaries(package_dir, runtime_dir, args.with_tests)

    runtime_data_root = runtime_dir / "data_root"
    if runtime_data_root.is_dir():
        log("Copying data_root from runtime...")
        copytree_contents(runtime_data_root, package_dir / "data_root")

    if dist_dir is not None:
        webui_dest = package_dir / "data_root" / "webui"
        copytree_contents(dist_dir, webui_dest)
        log(f"  WebUI copied to {webui_dest}")

    write_default_config(package_dir)
    log("Copying release launcher scripts...")
    copy_release_script_files(package_dir)
    manifest_path = write_manifest(
        package_dir,
        package_name,
        build_dir_abs,
        args.with_tests,
        args.skip_webui,
        args.skip_tests,
    )
    run_duplicate_check(package_dir)

    log("")
    log("=== Release package created ===")
    log(f"  Package : {package_dir}")
    log(f"  Manifest: {manifest_path}")
    log("")
    log("To start the server:")
    log(f"  cd {package_dir}")
    if is_windows():
        log("  .\\start.bat              (batch wrapper -> PowerShell)")
        log("  .\\start.ps1              (PowerShell)")
        log("  .\\start.bat --port=6200  (custom port)")
        log("")
        log("For development (with driver aliases):")
        log("  .\\dev.bat                (batch wrapper -> interactive PowerShell)")
        log("  .\\dev.ps1                (PowerShell with configured environment)")
    else:
        log("  ./start.sh              (bash)")
        log("  ./start.sh --port=6200  (custom port)")
        log("")
        log("To install as a Linux service (systemd):")
        log("  sudo ./install_service.sh")
        log("  sudo ./uninstall_service.sh")
        log("")
        log("For development (with driver aliases):")
        log("  ./dev.sh                (interactive bash with configured environment)")


def test_runtime(args: argparse.Namespace) -> None:
    assert_required_ports_available()
    assert_no_blocking_processes()

    config = normalize_config(args.config)
    build_runtime(build_dir_arg=args.build_dir, config=config, clean_runtime=False)
    suites = selected_suites(args)
    run_selected_tests(args.build_dir, config, suites)


def build_only(args: argparse.Namespace) -> None:
    config = normalize_config(args.config)
    build_runtime(build_dir_arg=args.build_dir, config=config, clean_runtime=False)


def add_suite_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--gtest", action="store_true", help="Run only GTest (C++) tests")
    parser.add_argument("--smoke", action="store_true", help="Run only smoke tests (Python)")
    parser.add_argument("--vitest", action="store_true", help="Run only Vitest (WebUI unit) tests")
    parser.add_argument("--playwright", action="store_true", help="Run only Playwright (E2E) tests")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Cross-platform build/test/publish entry for stdiolink.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_cmd = subparsers.add_parser("build", help="Compile the latest binaries")
    build_cmd.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    build_cmd.add_argument("--config", default="release", help="Build config: debug or release (default: release)")
    build_cmd.set_defaults(handler=build_only)

    test_cmd = subparsers.add_parser("test", help="Build then run selected test suites")
    test_cmd.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    test_cmd.add_argument("--config", default="release", help="Build config: debug or release (default: release)")
    add_suite_flags(test_cmd)
    test_cmd.set_defaults(handler=test_runtime)

    publish_cmd = subparsers.add_parser("publish", help="Build, optionally test, then create a release package")
    publish_cmd.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    publish_cmd.add_argument("--output-dir", default="release", help="Release output root (default: release)")
    publish_cmd.add_argument("--name", default="", help="Package name (default: stdiolink_<timestamp>_<git>)")
    publish_cmd.add_argument("--with-tests", action="store_true", help="Include test binaries in bin/")
    publish_cmd.add_argument("--skip-build", action="store_true", help="Skip C++ build (assume runtime_release already exists)")
    publish_cmd.add_argument("--skip-webui", action="store_true", help="Skip WebUI build")
    publish_cmd.add_argument("--skip-tests", action="store_true", help="Skip test execution before packaging")
    add_suite_flags(publish_cmd)
    publish_cmd.set_defaults(handler=package_release)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.handler(args)
        return 0
    except ReleaseError as exc:
        print(f"Error: {exc}", file=sys.stderr, flush=True)
        return 1
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr, flush=True)
        return 130


if __name__ == "__main__":
    sys.exit(main())

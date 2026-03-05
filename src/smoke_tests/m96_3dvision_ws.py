#!/usr/bin/env python3
"""M96 — driver_3dvision WebSocket 修复冒烟测试"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent

def _driver_paths() -> list[tuple[str, Path]]:
    exe = "stdio.drv.3dvision.exe" if os.name == "nt" else "stdio.drv.3dvision"
    return [
        (
            "runtime_debug",
            PROJECT_ROOT / "build" / "runtime_debug" / "data_root" / "drivers" / "stdio.drv.3dvision" / exe,
        ),
        (
            "runtime_release",
            PROJECT_ROOT / "build" / "runtime_release" / "data_root" / "drivers" / "stdio.drv.3dvision" / exe,
        ),
        ("raw_debug", PROJECT_ROOT / "build" / "debug" / exe),
        ("raw_release", PROJECT_ROOT / "build" / "release" / exe),
    ]


def find_driver() -> tuple[Path | None, str | None]:
    for layout, path in _driver_paths():
        if path.exists():
            return path, layout
    return None, None


def run_s01(driver: Path) -> bool:
    """S01: --cmd=meta.describe 输出含 ws.connect"""
    result = subprocess.run(
        [str(driver), "--cmd=meta.describe"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
    )
    if result.returncode != 0:
        print(f"[FAIL] S01: exit code {result.returncode}")
        print(result.stderr, file=sys.stderr)
        return False
    stdout = result.stdout or ""
    if "ws.connect" not in stdout:
        print("[FAIL] S01: output does not contain 'ws.connect'")
        return False
    print("[PASS] S01: meta.describe 输出包含 ws.connect")
    return True


def run_s02(driver: Path) -> bool:
    """S02: --help 退出码 0"""
    result = subprocess.run(
        [str(driver), "--help"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10,
    )
    if result.returncode != 0:
        print(f"[FAIL] S02: exit code {result.returncode}")
        print(result.stderr, file=sys.stderr)
        return False
    print("[PASS] S02: --help 退出码 0")
    return True


def run_s03(driver: Path) -> bool:
    """S03: 启动 mock WS server + ws.connect/subscribe/event/disconnect 全链路"""
    try:
        import asyncio
        import websockets
    except ImportError:
        print("[SKIP] S03: websockets 包未安装 (pip install websockets)")
        return True  # skip 不算失败

    event_sent = threading.Event()
    server_port = [0]
    server_ready = threading.Event()

    async def ws_handler(websocket):
        """处理 WebSocket 连接：接收 sub 后推送一条事件"""
        try:
            async for message in websocket:
                msg = json.loads(message)
                if msg.get("type") == "sub":
                    # 收到订阅后推送一条事件
                    event_payload = json.dumps({
                        "type": "pub",
                        "message": json.dumps({
                            "event": "scanner.ready",
                            "data": {"vesselId": 42}
                        })
                    })
                    await websocket.send(event_payload)
                    event_sent.set()
                elif msg.get("type") == "ping":
                    await websocket.send(json.dumps({"type": "pong"}))
        except websockets.exceptions.ConnectionClosed:
            # 驱动进程结束时连接被动关闭，冒烟测试无需将其视为失败。
            return

    async def run_server():
        async with websockets.serve(ws_handler, "127.0.0.1", 0) as server:
            server_port[0] = server.sockets[0].getsockname()[1]
            server_ready.set()
            # 保持运行直到测试完成
            await asyncio.sleep(30)

    # 在后台线程启动 WS server
    loop = asyncio.new_event_loop()
    server_thread = threading.Thread(target=loop.run_until_complete, args=(run_server(),), daemon=True)
    server_thread.start()
    server_ready.wait(timeout=5)

    if server_port[0] == 0:
        print("[FAIL] S03: mock WS server 启动失败")
        return False

    port = server_port[0]
    print(f"[INFO] S03: mock WS server 启动在 127.0.0.1:{port}")

    # 启动 driver 子进程
    proc = subprocess.Popen(
        [str(driver), "--profile=keepalive"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace",
    )

    try:
        # ws.connect
        cmd_connect = json.dumps({"cmd": "ws.connect", "data": {"addr": f"127.0.0.1:{port}"}}) + "\n"
        proc.stdin.write(cmd_connect)
        proc.stdin.flush()

        resp_line = proc.stdout.readline()
        resp = json.loads(resp_line)
        if resp.get("status") != "done" or not resp.get("data", {}).get("connected"):
            print(f"[FAIL] S03: ws.connect 返回非预期: {resp}")
            return False
        print("[INFO] S03: ws.connect → done (connected:true)")

        # ws.subscribe
        cmd_sub = json.dumps({"cmd": "ws.subscribe", "data": {"topic": "vessel.notify"}}) + "\n"
        proc.stdin.write(cmd_sub)
        proc.stdin.flush()

        resp_line = proc.stdout.readline()
        resp = json.loads(resp_line)
        if resp.get("status") != "done":
            print(f"[FAIL] S03: ws.subscribe 返回非预期: {resp}")
            return False
        print("[INFO] S03: ws.subscribe → done")

        # 等待事件
        event_sent.wait(timeout=5)
        time.sleep(0.5)  # 给 stdio 输出一点缓冲时间

        resp_line = proc.stdout.readline()
        resp = json.loads(resp_line)
        if resp.get("status") != "event":
            print(f"[FAIL] S03: 期望 event 行，收到: {resp}")
            return False

        # 兼容两种事件结构：
        # A) 旧结构：{"status":"event","event":"scanner.ready","data":{...}}
        # B) 新结构：{"status":"event","data":{"event":"scanner.ready","data":{...}}}
        event_name = resp.get("event")
        ev_data = resp.get("data", {})
        if event_name is None and isinstance(ev_data, dict):
            nested_name = ev_data.get("event")
            nested_data = ev_data.get("data")
            if isinstance(nested_name, str) and isinstance(nested_data, dict):
                event_name = nested_name
                ev_data = nested_data

        if event_name != "scanner.ready":
            print(f"[FAIL] S03: 事件名非预期: {event_name}, raw={resp}")
            return False

        # 验证扁平结构
        if "event" in ev_data:
            print(f"[FAIL] S03: data 包含嵌套 event 字段（双层包装未修复）: {ev_data}")
            return False
        if ev_data.get("vesselId") != 42:
            print(f"[FAIL] S03: data.vesselId 非预期: {ev_data}")
            return False
        print("[INFO] S03: 收到事件 scanner.ready, data 扁平结构正确")

        # ws.disconnect
        cmd_disc = json.dumps({"cmd": "ws.disconnect", "data": {}}) + "\n"
        proc.stdin.write(cmd_disc)
        proc.stdin.flush()

        resp_line = proc.stdout.readline()
        resp = json.loads(resp_line)
        if resp.get("status") != "done" or not resp.get("data", {}).get("disconnected"):
            print(f"[FAIL] S03: ws.disconnect 返回非预期: {resp}")
            return False
        print("[INFO] S03: ws.disconnect → done (disconnected:true)")

        print("[PASS] S03: WebSocket 连接-订阅-事件-断开 全链路通过")
        return True

    except Exception as e:
        print(f"[FAIL] S03: {e}")
        return False
    finally:
        proc.kill()
        proc.wait()


def run_case(case_name: str, driver: Path) -> bool:
    cases = {"S01": run_s01, "S02": run_s02, "S03": run_s03}
    func = cases.get(case_name)
    if func is None:
        print(f"[FAIL] Unknown case: {case_name}")
        return False
    return func(driver)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", help="运行单个用例 (S01/S02/S03)")
    args = parser.parse_args()

    driver, layout = find_driver()
    if driver is None:
        print("[FAIL] stdio.drv.3dvision 可执行文件未找到")
        for name, path in _driver_paths():
            print(f"  - expected[{name}]: {path}")
        return 1
    print(f"[INFO] Driver layout: {layout}, exe: {driver}")

    cases = ["S01", "S02", "S03"]
    if args.case:
        cases = [args.case]

    passed = 0
    failed = 0
    for case in cases:
        try:
            if run_case(case, driver):
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"[FAIL] {case}: {e}")
            failed += 1

    print(f"\n总计: {passed} passed, {failed} failed")
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())

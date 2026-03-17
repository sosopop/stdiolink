#!/usr/bin/env python3
from __future__ import annotations

import socket


# Modify this list to test the ports you care about.
PORTS_TO_TEST = [3000, 3001, 3002, 3003, 6119, 6200, 6201, 6202]


def test_bind(host: str, family: socket.AddressFamily, port: int) -> None:
    sock = socket.socket(family, socket.SOCK_STREAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if family == socket.AF_INET6:
            sock.bind((host, port, 0, 0))
        else:
            sock.bind((host, port))
        print(f"[OK]   {host}:{port}")
    except OSError as exc:
        print(
            f"[FAIL] {host}:{port} "
            f"(errno={exc.errno}, winerror={getattr(exc, 'winerror', None)}) {exc}"
        )
    finally:
        sock.close()


def main() -> int:
    hosts: list[tuple[str, socket.AddressFamily]] = [("127.0.0.1", socket.AF_INET)]
    if socket.has_ipv6:
        hosts.append(("::1", socket.AF_INET6))

    for port in PORTS_TO_TEST:
        print(f"=== Port {port} ===")
        for host, family in hosts:
            test_bind(host, family, port)
        print("")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

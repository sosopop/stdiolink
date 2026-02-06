#!/usr/bin/env python3
"""Bidirectional TCP <-> serial bridge."""

from __future__ import annotations

import argparse
import logging
import socket
import threading
import time
from contextlib import contextmanager
from typing import Iterator, Tuple

import serial


BUFFER_SIZE = 4096


def _build_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(
		description=(
			"Bridge bytes between a TCP endpoint and a serial port. "
			"Serial writes are forwarded to the socket while TCP data "
			"is written back to the serial line."
		),
		formatter_class=argparse.RawDescriptionHelpFormatter,
		epilog=(
			"Examples:\n"
			"  # Act as a TCP server on port 9000, forwarding to /dev/ttyUSB0\n"
			"  python3 tcp_bridge.py /dev/ttyUSB0 9000 --mode server\n\n"
			"  # Connect as a TCP client to 192.168.1.50:9000 using COM3\n"
			"  python3 tcp_bridge.py COM3 9000 --host 192.168.1.50 --mode client"
		),
	)
	parser.add_argument("serial_port", help="Serial port path, e.g. /dev/ttyUSB0 or COM3")
	parser.add_argument("port", type=int, help="TCP port to listen on (server) or connect to (client)")
	parser.add_argument(
		"--host",
		default="0.0.0.0",
		help="TCP host/IP. Acts as bind address in server mode or remote host in client mode",
	)
	parser.add_argument(
		"--mode",
		choices=("server", "client"),
		default="server",
		help="`server` accepts a connection, `client` connects to an upstream TCP server",
	)
	parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate")
	parser.add_argument("--bytesize", type=int, choices=(5, 6, 7, 8), default=8, help="Serial data bits")
	parser.add_argument(
		"--parity",
		choices=("N", "E", "O", "M", "S"),
		default="N",
		help="Serial parity (default: N)",
	)
	parser.add_argument(
		"--stopbits",
		type=float,
		choices=(1, 1.5, 2),
		default=1,
		help="Serial stop bits",
	)
	parser.add_argument("--xonxoff", action="store_true", help="Enable XON/XOFF flow control")
	parser.add_argument("--rtscts", action="store_true", help="Enable RTS/CTS flow control")
	parser.add_argument("--dsrdtr", action="store_true", help="Enable DSR/DTR flow control")
	parser.add_argument(
		"--reconnect-delay",
		type=float,
		default=3.0,
		help="Seconds to wait before retrying TCP connection in client mode (0 disables retries)",
	)
	parser.add_argument(
		"--log-level",
		choices=("DEBUG", "INFO", "WARNING", "ERROR"),
		default="INFO",
		help="Logging verbosity",
	)
	return parser


def _serial_kwargs(args: argparse.Namespace) -> dict:
	bytesize_map = {
		5: serial.FIVEBITS,
		6: serial.SIXBITS,
		7: serial.SEVENBITS,
		8: serial.EIGHTBITS,
	}
	stopbits_map = {
		1: serial.STOPBITS_ONE,
		1.5: serial.STOPBITS_ONE_POINT_FIVE,
		2: serial.STOPBITS_TWO,
	}
	return {
		"port": args.serial_port,
		"baudrate": args.baudrate,
		"bytesize": bytesize_map[args.bytesize],
		"parity": args.parity,
		"stopbits": stopbits_map[args.stopbits],
		"xonxoff": args.xonxoff,
		"rtscts": args.rtscts,
		"dsrdtr": args.dsrdtr,
		"timeout": 0.1,
	}


@contextmanager
def open_serial(args: argparse.Namespace) -> Iterator[serial.Serial]:
	ser = serial.Serial(**_serial_kwargs(args))
	logging.info(
		"Opened serial %s @ %s bps, bytesize=%s parity=%s stopbits=%s",
		ser.port,
		ser.baudrate,
		ser.bytesize,
		ser.parity,
		ser.stopbits,
	)
	try:
		yield ser
	finally:
		ser.close()
		logging.info("Closed serial port")


def forward_serial_to_socket(ser: serial.Serial, sock: socket.socket, stop_event: threading.Event) -> None:
	while not stop_event.is_set():
		try:
			available = ser.in_waiting
			payload = ser.read(available if available > 0 else 1)
		except serial.SerialException as exc:
			logging.error("Serial read error: %s", exc)
			stop_event.set()
			break
		if payload:
			try:
				sock.sendall(payload)
			except OSError as exc:
				logging.error("Socket send error: %s", exc)
				stop_event.set()
				break


def forward_socket_to_serial(sock: socket.socket, ser: serial.Serial, stop_event: threading.Event) -> None:
	sock.settimeout(0.5)
	while not stop_event.is_set():
		try:
			chunk = sock.recv(BUFFER_SIZE)
			if not chunk:
				stop_event.set()
				break
			ser.write(chunk)
		except socket.timeout:
			continue
		except (OSError, serial.SerialException) as exc:
			logging.error("Socket/serial error: %s", exc)
			stop_event.set()
			break


def pump_connection(conn: socket.socket, peer: Tuple[str, int], ser: serial.Serial) -> None:
	logging.info("TCP peer %s:%s connected", *peer)
	stop_event = threading.Event()
	threads = [
		threading.Thread(target=forward_serial_to_socket, args=(ser, conn, stop_event), daemon=True),
		threading.Thread(target=forward_socket_to_serial, args=(conn, ser, stop_event), daemon=True),
	]
	for thread in threads:
		thread.start()
	for thread in threads:
		thread.join()
	conn.close()
	logging.info("TCP peer %s:%s disconnected", *peer)


def run_server(args: argparse.Namespace, ser: serial.Serial) -> None:
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
		server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		server.bind((args.host, args.port))
		server.listen(1)
		logging.info("Listening on %s:%s", args.host, args.port)
		while True:
			conn, peer = server.accept()
			pump_connection(conn, peer, ser)


def run_client(args: argparse.Namespace, ser: serial.Serial) -> None:
	while True:
		try:
			with socket.create_connection((args.host, args.port)) as conn:
				pump_connection(conn, (args.host, args.port), ser)
		except OSError as exc:
			logging.error("TCP connection error: %s", exc)
			if args.reconnect_delay <= 0:
				break
			logging.info("Reconnecting in %.1fs", args.reconnect_delay)
			time.sleep(args.reconnect_delay)
			continue
		if args.reconnect_delay <= 0:
			break
		if args.reconnect_delay > 0:
			logging.info("Re-establishing TCP connection in %.1fs", args.reconnect_delay)
			time.sleep(args.reconnect_delay)


def main() -> None:
	parser = _build_parser()
	args = parser.parse_args()
	logging.basicConfig(level=getattr(logging, args.log_level), format="[%(asctime)s] %(levelname)s: %(message)s")
	with open_serial(args) as ser:
		try:
			if args.mode == "server":
				run_server(args, ser)
			else:
				run_client(args, ser)
		except KeyboardInterrupt:
			logging.info("Interrupted, shutting down")


if __name__ == "__main__":
	main()

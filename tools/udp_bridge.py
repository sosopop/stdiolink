#!/usr/bin/env python3
"""Bidirectional UDP <-> serial bridge."""

from __future__ import annotations

import argparse
import logging
import socket
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Iterator, Optional, Tuple

import serial


MAX_DATAGRAM = 65535


def _build_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(
		description=(
			"Send UDP payloads to a serial port while mirroring serial bytes back to "
			"a UDP peer. When no explicit target is provided, the bridge will send "
			"serial data to the most recent UDP sender."
		),
		formatter_class=argparse.RawDescriptionHelpFormatter,
		epilog=(
			"Examples:\n"
			"  # Listen on UDP/5000 and forward responses to last sender\n"
			"  python3 udp_bridge.py /dev/ttyUSB0 5000\n\n"
			"  # Force serial data to always go to 192.168.1.100:6000\n"
			"  python3 udp_bridge.py COM4 5000 --target-host 192.168.1.100 --target-port 6000"
		),
	)
	parser.add_argument("serial_port", help="Serial port path, e.g. /dev/ttyUSB0 or COM3")
	parser.add_argument("listen_port", type=int, help="UDP port to bind for inbound packets")
	parser.add_argument("--listen-host", default="0.0.0.0", help="UDP host/IP to bind (default: 0.0.0.0)")
	parser.add_argument("--target-host", help="Send serial bytes to this host instead of the last sender")
	parser.add_argument("--target-port", type=int, help="Send serial bytes to this port (required if target-host is set)")
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
		"--log-level",
		choices=("DEBUG", "INFO", "WARNING", "ERROR"),
		default="INFO",
		help="Logging verbosity",
	)
	parser.add_argument(
		"--serial-timeout",
		type=float,
		default=0.1,
		help="Seconds to wait for serial reads (keeps the loop responsive)",
	)
	parser.add_argument(
		"--idle-log",
		type=float,
		default=0,
		help="Optional seconds between repeating 'waiting for UDP peer' logs",
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
		"timeout": args.serial_timeout,
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


@dataclass
class PeerTracker:
	peer: Optional[Tuple[str, int]] = None
	lock: threading.Lock = field(default_factory=threading.Lock)

	def set(self, peer: Tuple[str, int]) -> None:
		with self.lock:
			self.peer = peer

	def get(self) -> Optional[Tuple[str, int]]:
		with self.lock:
			return self.peer


def udp_to_serial(sock: socket.socket, ser: serial.Serial, tracker: PeerTracker, stop_event: threading.Event) -> None:
	sock.settimeout(0.5)
	while not stop_event.is_set():
		try:
			payload, addr = sock.recvfrom(MAX_DATAGRAM)
		except socket.timeout:
			continue
		except OSError as exc:
			logging.error("UDP receive error: %s", exc)
			stop_event.set()
			break
		if not payload:
			continue
		tracker.set(addr)
		try:
			ser.write(payload)
		except serial.SerialException as exc:
			logging.error("Serial write error: %s", exc)
			stop_event.set()
			break


def serial_to_udp(
	ser: serial.Serial,
	sock: socket.socket,
	tracker: PeerTracker,
	stop_event: threading.Event,
	idle_log_interval: float,
) -> None:
	last_idle_log = 0.0
	while not stop_event.is_set():
		try:
			available = ser.in_waiting
			data = ser.read(available if available > 0 else 1)
		except serial.SerialException as exc:
			logging.error("Serial read error: %s", exc)
			stop_event.set()
			break
		if not data:
			if idle_log_interval > 0:
				now = time.time()
				if now - last_idle_log >= idle_log_interval:
					last_idle_log = now
					if tracker.get() is None:
						logging.info("Waiting for UDP peer before forwarding serial data")
			continue
		target = tracker.get()
		if target is None:
			logging.debug("Dropping %d serial bytes (no UDP peer yet)", len(data))
			continue
		try:
			sock.sendto(data, target)
		except OSError as exc:
			logging.error("UDP send error: %s", exc)
			stop_event.set()
			break


def run(args: argparse.Namespace) -> None:
	if args.target_host and not args.target_port:
		raise SystemExit("--target-port is required when --target-host is provided")
	tracker = PeerTracker()
	if args.target_host and args.target_port:
		tracker.set((args.target_host, args.target_port))
	with open_serial(args) as ser, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
		sock.bind((args.listen_host, args.listen_port))
		logging.info("Listening for UDP packets on %s:%s", args.listen_host, args.listen_port)
		stop_event = threading.Event()
		serial_thread = threading.Thread(
			target=serial_to_udp,
			args=(ser, sock, tracker, stop_event, args.idle_log),
			daemon=True,
		)
		udp_thread = threading.Thread(target=udp_to_serial, args=(sock, ser, tracker, stop_event), daemon=True)
		serial_thread.start()
		udp_thread.start()
		try:
			while serial_thread.is_alive() and udp_thread.is_alive():
				time.sleep(0.2)
		except KeyboardInterrupt:
			logging.info("Interrupted, shutting down")
		finally:
			stop_event.set()
			serial_thread.join()
			udp_thread.join()


def main() -> None:
	parser = _build_parser()
	args = parser.parse_args()
	logging.basicConfig(level=getattr(logging, args.log_level), format="[%(asctime)s] %(levelname)s: %(message)s")
	run(args)


if __name__ == "__main__":
	main()

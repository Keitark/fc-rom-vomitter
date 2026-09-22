#!/usr/bin/env python3
"""Upload one iNES image to FC ROM Vomitter over native USB Serial/JTAG."""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path


MAGIC = b"RVUP"
VERSION = 1
HEADER_SIZE = 16
MAX_INES_SIZE = 16 + 512 + 32 * 1024 + 8 * 1024


def build_frame(payload: bytes) -> bytes:
    if len(payload) < 16 or len(payload) > MAX_INES_SIZE:
        raise ValueError(f"ROM size must be 16..{MAX_INES_SIZE} bytes")
    if payload[:4] != b"NES\x1a":
        raise ValueError("file is not an iNES image (missing NES 1A magic)")
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack("<4sBBHII", MAGIC, VERSION, 0, HEADER_SIZE, len(payload), crc)
    return header + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB Serial/JTAG port, for example COM10 or /dev/ttyACM0")
    parser.add_argument("rom", type=Path, help="mapper-0 iNES file")
    parser.add_argument("--timeout", type=float, default=20.0, help="reply timeout in seconds")
    parser.add_argument("--dry-run", action="store_true", help="validate and print frame details only")
    args = parser.parse_args()

    payload = args.rom.read_bytes()
    try:
        frame = build_frame(payload)
    except ValueError as error:
        parser.error(str(error))
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    print(f"iNES bytes={len(payload)} crc32={crc:08x}")
    if args.dry_run:
        return 0

    try:
        import serial
    except ImportError:
        print("pyserial is required: py -m pip install pyserial", file=sys.stderr)
        return 2

    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200  # USB transport ignores UART baud rate.
    connection.timeout = args.timeout
    connection.write_timeout = args.timeout
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        connection.reset_input_buffer()
        connection.write(frame)
        connection.flush()
        response = connection.readline().decode("utf-8", errors="replace").strip()
    finally:
        connection.close()

    if not response:
        print("device did not reply before timeout", file=sys.stderr)
        return 3
    print(response)
    return 0 if response.startswith("RVOK ") else 1


if __name__ == "__main__":
    raise SystemExit(main())

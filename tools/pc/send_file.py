#!/usr/bin/env python3
"""Send a file to STM32 over USB CDC and save it to SD card."""

from __future__ import annotations

import argparse
import binascii
import os
import sys
import time
from pathlib import Path

import serial


DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 10.0
DEFAULT_DUMP_BYTES = 64


def crc32_update(crc: int, data: bytes) -> int:
    return binascii.crc32(data, crc) & 0xFFFFFFFF


def file_crc32(path: Path) -> int:
    crc = 0
    with path.open("rb") as file:
        while True:
            data = file.read(65536)
            if not data:
                break
            crc = crc32_update(crc, data)
    return crc


def dump_bytes(prefix: str, data: bytes, limit: int) -> None:
    if limit < 0:
        return

    dump_len = len(data) if limit == 0 else min(len(data), limit)
    shown = data[:dump_len]
    hex_text = " ".join(f"{byte:02X}" for byte in shown)
    ascii_text = "".join(chr(byte) if 32 <= byte <= 126 else "." for byte in shown)
    suffix = "" if dump_len == len(data) else " ..."
    print(f"{prefix} len={len(data)} dump={dump_len}: {hex_text}{suffix}")
    print(f"{prefix} ascii: {ascii_text}{suffix}")


def send_line(port: serial.Serial, line: str, dump_limit: int) -> None:
    data = (line + "\n").encode("ascii")
    dump_bytes("TX line", data, dump_limit)
    port.write(data)


def read_line(port: serial.Serial) -> str:
    line = port.readline()
    if not line:
        raise TimeoutError("timeout waiting for STM32 response")
    return line.decode("ascii", errors="replace").strip()


def require_safe_filename(path: Path) -> str:
    name = path.name
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.")
    parts = name.split(".")

    if not name or len(name.encode("ascii", errors="ignore")) >= 64:
        raise ValueError("filename must be 1..63 ASCII bytes")
    if ".." in name:
        raise ValueError("filename must not contain '..'")
    if any(ch not in allowed for ch in name):
        raise ValueError("filename allows only A-Z a-z 0-9 _ - .")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8:
        raise ValueError("current firmware requires FAT 8.3 filename, for example H7REC.HEX")
    if len(parts) == 2 and (not parts[1] or len(parts[1]) > 3):
        raise ValueError("current firmware requires FAT 8.3 filename, for example H7REC.HEX")

    return name


def send_file(port_name: str, path: Path, baudrate: int, timeout: float, dump_limit: int) -> None:
    filename = require_safe_filename(path)
    size = path.stat().st_size
    crc = file_crc32(path)

    print(f"open {port_name}, send {filename}, size={size}, crc={crc:08X}")

    with serial.Serial(port_name, baudrate=baudrate, timeout=timeout, write_timeout=timeout) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        time.sleep(0.1)

        send_line(port, f"PUT {filename} {size} {crc:08X}", dump_limit)
        response = read_line(port)
        print("<", response)
        if not response.startswith("READY "):
            raise RuntimeError(response)

        chunk_size = int(response.split()[1])
        if chunk_size <= 0:
            raise RuntimeError(f"invalid chunk size: {chunk_size}")

        sent = 0
        block_index = 0
        start_time = time.monotonic()
        with path.open("rb") as file:
            while True:
                payload = file.read(chunk_size)
                if not payload:
                    break

                block_crc = crc32_update(0, payload)
                send_line(port, f"BLK {block_index} {len(payload)} {block_crc:08X}", dump_limit)
                dump_bytes("TX data", payload, dump_limit)
                port.write(payload)
                port.flush()

                response = read_line(port)
                if response != f"ACK {block_index}":
                    raise RuntimeError(response)

                sent += len(payload)
                block_index += 1
                elapsed = max(time.monotonic() - start_time, 0.001)
                speed_kib = (sent / 1024.0) / elapsed
                percent = (sent * 100.0 / size) if size else 100.0
                print(
                    f"\r{sent}/{size} bytes ({percent:5.1f}%) {speed_kib:7.1f} KiB/s",
                    end="",
                    flush=True,
                )

        print()
        print("send END, waiting DONE...")
        send_line(port, "END", dump_limit)
        response = read_line(port)
        print("<", response)
        if not response.startswith("DONE "):
            raise RuntimeError(response)

        elapsed = max(time.monotonic() - start_time, 0.001)
        speed_kib = (sent / 1024.0) / elapsed
        print(f"done in {elapsed:.2f}s, avg {speed_kib:.1f} KiB/s")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Send a file to STM32 SD card via USB CDC")
    parser.add_argument("port", help="serial port, for example COM7 or /dev/ttyACM0")
    parser.add_argument("file", type=Path, help="file to send")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument(
        "--dump-bytes",
        type=int,
        default=DEFAULT_DUMP_BYTES,
        help="bytes to dump for each TX line/data block; 0 dumps all, negative disables",
    )
    args = parser.parse_args(argv)

    if not args.file.is_file():
        print(f"not a file: {args.file}", file=sys.stderr)
        return 2

    try:
        send_file(args.port, args.file, args.baudrate, args.timeout, args.dump_bytes)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

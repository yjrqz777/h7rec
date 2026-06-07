#!/usr/bin/env python3
"""
PC 端文件发送工具 — 通过 USB CDC 虚拟串口将文件传输到 STM32 并保存至 SD 卡。

通信协议（与下位机 CherryUSB CDC 固件配合）：
  1. PUT <filename> <size> <crc32>   → 发起文件传输请求
  2. READY <chunk_size>              ← 下位机回复，协商分块大小
  3. BLK <index> <len> <crc32>       → 发送数据块头部
  4. <raw binary payload>            → 发送数据块本体
  5. ACK <index>                     ← 下位机确认收到
  6. 重复 3~5 直到文件发送完毕
  7. END                             → 通知传输结束
  8. DONE <result>                   ← 下位机回复写入结果

使用示例:
  python send_file.py COM7 firmware.hex
  python send_file.py /dev/ttyACM0 H7REC.HEX --dump-bytes 32
"""

from __future__ import annotations

import argparse
import binascii
import os
import sys
import time
from pathlib import Path

import serial


# ======================== 默认参数 ========================

DEFAULT_BAUDRATE = 115200    # 串口波特率
DEFAULT_TIMEOUT = 30.0       # 读写超时（秒）
DEFAULT_DUMP_BYTES = -1      # 调试打印时显示前 N 字节；0 = 全部，负值 = 禁用


# ======================== CRC32 工具函数 ========================


def crc32_update(crc: int, data: bytes) -> int:
    """增量更新 CRC32 校验值。

    Args:
        crc:   上一次的 CRC32 值（初始为 0）。
        data:  参与校验的新数据。
    Returns:
        更新后的 CRC32 值（32 位无符号整数）。
    """
    return binascii.crc32(data, crc) & 0xFFFFFFFF


def file_crc32(path: Path) -> int:
    """计算文件的完整 CRC32 校验值。

    以 64 KiB 分块读取文件，增量计算 CRC32。
    用于在发送前将 CRC 告知下位机，下位机写入完成后比对校验。

    Args:
        path: 文件路径。
    Returns:
        文件全量的 CRC32 值。
    """
    crc = 0
    with path.open("rb") as file:
        while True:
            data = file.read(65536)
            if not data:
                break
            crc = crc32_update(crc, data)
    return crc


# ======================== 串口通信辅助函数 ========================


def dump_bytes(prefix: str, data: bytes, limit: int) -> None:
    """以十六进制和 ASCII 两种形式打印字节数据，用于调试。

    Args:
        prefix:  每行输出的前缀字符串。
        data:    待打印的字节数据。
        limit:   最大显示字节数。0 = 全部显示，负值 = 禁用打印。
    """
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
    """向串口发送一行 ASCII 文本命令（自动追加换行符）。

    文本命令包括 PUT / BLK / END 等协议控制指令。

    Args:
        port:       pyserial 串口对象。
        line:       待发送的命令字符串（不含换行符）。
        dump_limit: 调试打印限制字节数。
    """
    data = (line + "\n").encode("ascii")
    dump_bytes("TX line", data, dump_limit)
    port.write(data)


def read_line(port: serial.Serial) -> str:
    """从串口读取一行 ASCII 文本响应。

    阻塞等待直到收到换行符或超时。

    Args:
        port: pyserial 串口对象。
    Returns:
        去除首尾空白后的响应字符串。
    Raises:
        TimeoutError: 超时未收到数据时抛出。
    """
    line = port.readline()
    if not line:
        raise TimeoutError("timeout waiting for STM32 response")
    return line.decode("ascii", errors="replace").strip()


def format_progress(sent: int, total: int, elapsed: float) -> str:
    """Format transfer progress with MB for large files and KB for small files."""
    percent = (sent * 100.0 / total) if total else 100.0
    speed_kib = (sent / 1024.0) / elapsed
    if total >= 1024 * 1024:
        sent_value = sent / (1024.0 * 1024.0)
        total_value = total / (1024.0 * 1024.0)
        return f"{sent_value:.2f}/{total_value:.2f} MB ({percent:5.1f}%) {speed_kib:7.1f} KiB/s"

    sent_value = sent / 1024.0
    total_value = total / 1024.0
    return f"{sent_value:.1f}/{total_value:.1f} KB ({percent:5.1f}%) {speed_kib:7.1f} KiB/s"


def format_average_speed(sent: int, elapsed: float) -> str:
    """Format average speed in KiB/s."""
    return f"{(sent / 1024.0) / elapsed:.1f} KiB/s"


# ======================== 文件名校验 ========================


def require_safe_filename(path: Path) -> str:
    """校验并返回安全的 FAT 8.3 文件名。

    下位机使用 FAT 文件系统保存文件到 SD 卡，文件名必须符合 8.3 格式
    （主名 ≤8 字符 + "." + 扩展名 ≤3 字符），且仅允许 ASCII 字母、数字、
    '_'、'-'、'.' 字符，总长度不超过 63 字节。

    Args:
        path: 文件路径。
    Returns:
        校验通过的文件名字符串。
    Raises:
        ValueError: 文件名不符合 FAT 8.3 规范时抛出。
    """
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


# ======================== 核心传输逻辑 ========================


def send_file(port_name: str, path: Path, baudrate: int, timeout: float, dump_limit: int) -> None:
    """通过 USB CDC 串口将文件传输到 STM32 并保存至 SD 卡。

    通信协议流程（每块等待 ACK）:
    ┌───────────────────────────────────────────────────────┐
    │  PC (发送方)                    STM32 (接收方)        │
    │  ───────────────────────────────────────────────────── │
    │  PUT <name> <size> <crc32>  ──→                        │
    │                               ←──  READY <chunk_size>  │
    │  BLK <0> <len> <crc>        ──→                        │
    │  <raw payload 0>            ──→                        │
    │  BLK <1> <len> <crc>        ──→   (等待 ACK 后继续)   │
    │  <raw payload 1>            ──→                        │
    │  ... 重复直到文件发完                                   │
    │  END                        ──→                        │
    │                               ←──  DONE <result>       │
    └───────────────────────────────────────────────────────┘

    Args:
        port_name:  串口设备名（如 COM7、/dev/ttyACM0）。
        path:       待发送的文件路径。
        baudrate:   串口波特率。
        timeout:    串口读写超时（秒）。
        dump_limit: 调试打印限制字节数。
    Raises:
        RuntimeError: 下位机返回错误响应或协议握手失败时抛出。
    """
    filename = require_safe_filename(path)
    size = path.stat().st_size
    crc = file_crc32(path)

    print(f"open {port_name}, send {filename}, size={size}, crc={crc:08X}")

    with serial.Serial(port_name, baudrate=baudrate, timeout=timeout, write_timeout=timeout) as port:
        # 清空串口缓冲区并等待稳定
        port.reset_input_buffer()
        port.reset_output_buffer()
        time.sleep(0.1)

        # ---- 第一步：握手，协商分块大小 ----
        send_line(port, f"PUT {filename} {size} {crc:08X}", dump_limit)
        response = read_line(port)
        print("<", response)
        if not response.startswith("READY "):
            raise RuntimeError(response)

        chunk_size = int(response.split()[1])
        if chunk_size <= 0:
            raise RuntimeError(f"invalid chunk size: {chunk_size}")

        # ---- 第二步：分块发送文件数据（每块等待 ACK） ----
        sent = 0
        block_index = 0
        start_time = time.monotonic()
        with path.open("rb") as file:
            while True:
                payload = file.read(chunk_size)
                if not payload:
                    break

                # 发送块头部（索引 / 长度 / CRC）
                block_crc = crc32_update(0, payload)
                send_line(port, f"BLK {block_index} {len(payload)} {block_crc:08X}", dump_limit)
                # 发送块数据本体（原始二进制）
                dump_bytes("TX data", payload, dump_limit)
                port.write(payload)
                port.flush()
                response = read_line(port)
                expected_ack = f"ACK {block_index}"
                if response != expected_ack:
                    raise RuntimeError(response)

                # 更新进度
                sent += len(payload)
                block_index += 1
                elapsed = max(time.monotonic() - start_time, 0.001)
                print(
                    f"\r{format_progress(sent, size, elapsed)}",
                    end="",
                    flush=True,
                )

        # ---- 第三步：通知传输结束 ----
        print()
        print("send END, waiting DONE...")
        send_line(port, "END", dump_limit)
        response = read_line(port)
        print("<", response)
        if not response.startswith("DONE "):
            raise RuntimeError(response)

        # 打印传输统计
        elapsed = max(time.monotonic() - start_time, 0.001)
        print(f"done in {elapsed:.2f}s, avg {format_average_speed(sent, elapsed)}")


# ======================== CLI 入口 ========================


def main(argv: list[str]) -> int:
    """命令行入口：解析参数并调用传输函数。

    Args:
        argv: 命令行参数列表（不含程序名）。
    Returns:
        0  成功；
        1  传输过程出错；
        2  指定的文件不存在。
    """
    parser = argparse.ArgumentParser(
        description="Send a file to STM32 SD card via USB CDC"
    )
    parser.add_argument("port", help="serial port, for example COM7 or /dev/ttyACM0")
    parser.add_argument("file", type=Path, help="file to send")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE,
                        help="serial baud rate (default: %(default)s)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help="serial read/write timeout in seconds (default: %(default)s)")
    parser.add_argument(
        "--dump-bytes",
        type=int,
        default=DEFAULT_DUMP_BYTES,
        help="bytes to dump per TX line/data block; "
             "0 = all, negative = disable (default: %(default)s)",
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
    """脚本入口点。"""
    raise SystemExit(main(sys.argv[1:]))

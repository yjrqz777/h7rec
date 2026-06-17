#!/usr/bin/env python3
"""把 Lottie/rlottie 的 JSON 文件转成 C 的 uint8_t 字节数组。

输出格式与项目里 rlottie_demo_json[] 一致：
  - 每行 12 个字节
  - 数组末尾追加 0x00 当作 C 字符串结尾
  - 同时生成 _len（不含结尾 0x00 的真实 JSON 长度，传给 rlottie 用这个）

用法:
    python json_to_c_array.py Welcome.jsn
    python json_to_c_array.py Welcome.jsn -o welcome.h -n welcome_json
    python json_to_c_array.py Welcome.jsn --minify        # 用 json 库压缩后再转

不带 -o 时直接打印到 stdout。
"""

import argparse
import json
import sys
from pathlib import Path


def to_c_array(data: bytes, name: str) -> str:
    lines = [f"const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        body = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {body},")
    lines.append("    0x00 /*Close the string*/")
    lines.append("};")
    lines.append("")
    lines.append(f"const unsigned int {name}_len = sizeof({name}) - 1;")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="JSON -> C uint8_t array (rlottie)")
    parser.add_argument("input", help="输入的 .json / .jsn 文件")
    parser.add_argument("-o", "--output", help="输出文件路径（默认打印到 stdout）")
    parser.add_argument("-n", "--name", default="rlottie_demo_json",
                        help="C 数组变量名（默认 rlottie_demo_json）")
    parser.add_argument("--minify", action="store_true",
                        help="先用 json 库去掉多余空白再转换")
    args = parser.parse_args()

    src = Path(args.input)
    if not src.is_file():
        print(f"找不到文件: {src}", file=sys.stderr)
        return 1

    if args.minify:
        # 解析再 dump，去掉空白；ensure_ascii=False 保留原始 UTF-8 字符
        obj = json.loads(src.read_text(encoding="utf-8"))
        data = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    else:
        data = src.read_bytes()

    out = to_c_array(data, args.name)

    if args.output:
        Path(args.output).write_text(out, encoding="utf-8")
        print(f"已写入 {args.output}（JSON {len(data)} 字节，数组 {len(data) + 1} 字节含结尾 0x00）",
              file=sys.stderr)
    else:
        sys.stdout.write(out)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

import argparse
import gzip
from pathlib import Path


def emit_header(data: bytes, symbol: str) -> str:
    lines = [
        "#pragma once",
        "",
        f"static const unsigned char {symbol}[] = {{",
    ]

    row = []
    for idx, byte in enumerate(data):
        row.append(f"0x{byte:02x}")
        if len(row) == 12 or idx == len(data) - 1:
            lines.append("    " + ", ".join(row) + ",")
            row = []

    lines.extend(
        [
            "};",
            "",
            f"static const unsigned int {symbol}_len = {len(data)};",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Gzip an asset and emit it as a C header.")
    parser.add_argument("--input", required=True, help="Input asset path")
    parser.add_argument("--output", required=True, help="Output header path")
    parser.add_argument("--symbol", required=True, help="C symbol prefix")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    raw = input_path.read_bytes()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    output_path.write_text(emit_header(gz, args.symbol), encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

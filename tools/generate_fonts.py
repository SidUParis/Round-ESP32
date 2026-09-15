#!/usr/bin/env python3
"""Regenerate the committed Noto UI subsets using lv_font_conv 1.5.3."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True, help="Noto Sans CJK SC regular OTF")
    parser.add_argument("--upstream", type=Path, default=Path.home() / ".cache/round-esp32/upstream")
    args = parser.parse_args()
    directory = ROOT / "firmware/round_shell"
    sources = list(directory.glob("*.cpp")) + list(directory.glob("*.hpp"))
    sources.append(args.upstream / "firmware/brookesia/components/XiaozhiApp/XiaozhiLocalization.cpp")
    symbols = "".join(sorted(set(re.findall(r"[^\x00-\x7f]", "".join(p.read_text() for p in sources)))))
    for size in (18, 24):
        output = directory / f"round_font_{size}.c"
        subprocess.run(["npx", "--yes", "lv_font_conv@1.5.3", "--font", str(args.font),
                        "--size", str(size), "--bpp", "4", "--format", "lvgl", "--range", "0x20-0x7e",
                        "--symbols", symbols, "--lv-font-name", f"round_font_{size}",
                        "--no-compress", "--output", str(output)], check=True)
        output.write_text("\n".join(line for line in output.read_text().splitlines()
                                    if "Opts:" not in line).rstrip() + "\n")
    (directory / "fonts-source.json").write_text(json.dumps({
        "family": "Noto Sans CJK SC", "license": "SIL OFL 1.1", "generator": "lv_font_conv 1.5.3",
        "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(), "sizes": [18, 24],
        "symbols": symbols
    }, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()

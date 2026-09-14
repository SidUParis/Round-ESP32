#!/usr/bin/env python3
"""Back up a 16 MiB ESP32-S3, flash only build artifacts, verify configuration preservation."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

FLASH_SIZE = 0x1000000
PROTECTED_START, PROTECTED_END = 0x9000, 0x110000
REQUIRED_OFFSETS = {0, 0x8000, 0x110000, 0x200000, 0xA00000}


def plan(build):
    build = Path(build).resolve()
    manifest = json.loads((build / "flasher_args.json").read_text())
    entries = []
    for offset, relative in manifest["flash_files"].items():
        offset = int(offset, 0)
        file = (build / relative).resolve()
        if not file.is_relative_to(build):
            raise ValueError("Manifest points outside the build directory")
        size = file.stat().st_size
        if size <= 0 or offset < 0 or offset + size > FLASH_SIZE:
            raise ValueError("Artifact exceeds the 16 MiB flash or is empty")
        if PROTECTED_START <= offset and offset + size <= PROTECTED_END:
            continue  # Never write NVS, factory configuration, PHY data or OTA state.
        if offset < PROTECTED_END and offset + size > PROTECTED_START:
            raise ValueError("Artifact crosses the protected configuration boundary")
        entries.append((offset, file, size))
    entries.sort()
    for left, right in zip(entries, entries[1:]):
        if left[0] + left[2] > right[0]:
            raise ValueError("Overlapping flash artifacts")
    if {offset for offset, _, _ in entries} != REQUIRED_OFFSETS:
        raise ValueError("Unexpected flash layout; expected the pinned Round partition layout")
    partition_file = next(file for offset, file, _ in entries if offset == 0x8000)
    validate_partitions(partition_file.read_bytes())
    return entries


def validate_partitions(data):
    entries = {}
    for start in range(0, len(data) - 31, 32):
        magic, kind, subtype, offset, size, name, flags = struct.unpack("<HBBII16sI", data[start:start+32])
        if magic != 0x50AA:
            break
        entries[name.rstrip(b"\0").decode("ascii")] = (offset, size)
    expected = {"nvsfactory": (0x9000, 0x32000), "nvs": (0x3B000, 0xD2000),
                "model": (0x110000, 0xF0000), "factory": (0x200000, 0x800000),
                "storage": (0xA00000, 0x600000)}
    for name, values in expected.items():
        if entries.get(name) != values:
            raise ValueError(f"Incompatible partition {name}: configuration preservation is not assured")


def esptool(port, *args):
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3",
                    "--port", port, "--baud", "460800", *map(str, args)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--backup-dir", type=Path,
                        default=Path.home() / ".local/share/round-esp32/backups")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    entries = plan(args.build_dir)
    print("Flash plan; 0x9000..0x110000 is preserved:", flush=True)
    for offset, file, size in entries:
        print(f"  {offset:#010x}  {size:>8} bytes  {file.name}", flush=True)
    if args.dry_run:
        return
    os.umask(0o077)
    args.backup_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup = args.backup_dir / f"before-round-{stamp}.bin"
    esptool(args.port, "flash-id")
    esptool(args.port, "read-flash", "0", hex(FLASH_SIZE), backup)
    original = backup.read_bytes()
    if len(original) != FLASH_SIZE:
        raise ValueError("Incomplete backup; refusing to flash")
    backup.with_suffix(".sha256").write_text(hashlib.sha256(original).hexdigest() + "  " + backup.name + "\n")
    command = ["write-flash", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB"]
    for offset, file, _ in entries:
        command.extend([hex(offset), str(file)])
    esptool(args.port, *command)
    check = args.backup_dir / f"after-config-{stamp}.bin"
    esptool(args.port, "read-flash", hex(PROTECTED_START), hex(PROTECTED_END-PROTECTED_START), check)
    if check.read_bytes() != original[PROTECTED_START:PROTECTED_END]:
        raise RuntimeError(f"Configuration verification failed. Original backup retained at {backup}")
    print(f"Flash complete; configuration region is byte-for-byte unchanged. Backup: {backup}")


if __name__ == "__main__":
    main()

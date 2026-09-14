#!/usr/bin/env python3
"""Package freshly built public artifacts, never a dump read from a user's board."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from flash import plan

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    entries = plan(args.build_dir)
    lock = json.loads((ROOT / "source.lock.json").read_text())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive = args.output_dir / f"round-esp32-{lock['version']}.zip"
    files = {hex(offset): f"firmware/{file.name}" for offset, file, _ in entries}
    manifest = {"flash_files": files, "source": lock,
                "sha256": {file.name: hashlib.sha256(file.read_bytes()).hexdigest()
                           for _, file, _ in entries}}
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for _, file, _ in entries:
            bundle.write(file, f"firmware/{file.name}")
        bundle.writestr("flasher_args.json", json.dumps(manifest, indent=2) + "\n")
        for name in ("flash.py", "device.py"):
            bundle.write(ROOT / "tools" / name, name)
        for name in ("LICENSE", "NOTICE", "README.md"):
            bundle.write(ROOT / name, name)
        bundle.write(ROOT / "firmware/round_shell/FONT-LICENSE.txt", "licenses/round-fonts.txt")
        description = json.loads((args.build_dir / "project_description.json").read_text())
        for directory in description.get("build_component_paths", []):
            directory = Path(directory)
            for file in directory.rglob("*"):
                if file.is_file() and file.name.upper().startswith(("LICENSE", "LICENCE", "COPYING")):
                    bundle.write(file, "licenses/" + directory.name + "/" + str(file.relative_to(directory)))
        bundle.writestr("INSTALL.txt", "Install esptool 5.4.0 and pyserial 3.5 in a Python environment.\n"
                        "From this extracted directory run:\n"
                        "python flash.py --build-dir . --port YOUR_PORT\n"
                        "This reads a private full backup before writing, and preserves configuration partitions.\n"
                        "Only use on the 16 MiB ESP32-S3-Touch-AMOLED-1.75.\n")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(digest + "  " + archive.name + "\n")
    print(archive)


if __name__ == "__main__":
    main()

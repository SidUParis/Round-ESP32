#!/usr/bin/env python3
"""Prepare a pinned upstream checkout plus the Round sources. No device writes."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SPARSE = ["firmware/brookesia", "examples/esp-idf/03_esp-brookesia/components/brookesia_core",
          "examples/esp-idf/03_esp-brookesia/components/brookesia_app_squareline_demo"]


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


def prepare(destination):
    lock = json.loads((ROOT / "source.lock.json").read_text())
    destination = destination.expanduser().absolute()
    if " " in str(destination):
        raise ValueError("ESP-IDF build workspace must not contain spaces; use the default cache path")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        run("git", "clone", "--filter=blob:none", "--depth=1", "--no-checkout",
            lock["upstream_url"], str(destination))
        run("git", "-C", str(destination), "fetch", "--depth=1", "origin", lock["upstream_commit"])
        run("git", "-C", str(destination), "sparse-checkout", "set", *SPARSE)
        run("git", "-C", str(destination), "checkout", "--detach", lock["upstream_commit"])
    current = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=destination, text=True).strip()
    if current != lock["upstream_commit"]:
        raise ValueError("Workspace has a different upstream revision; choose a new --destination")
    patch = ROOT / "patches/round-shell.patch"
    already = subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                             cwd=destination, capture_output=True).returncode == 0
    if not already:
        run("git", "apply", "--check", str(patch), cwd=destination)
        run("git", "apply", str(patch), cwd=destination)
    target = destination / "firmware/brookesia/components/round_shell"
    shutil.copytree(ROOT / "firmware/round_shell", target, dirs_exist_ok=True)
    print(destination / "firmware/brookesia")
    return destination / "firmware/brookesia"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    parser.add_argument("--destination", type=Path, default=cache / "round-esp32/upstream")
    try:
        prepare(parser.parse_args().destination)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Prepare failed: {error}\n")

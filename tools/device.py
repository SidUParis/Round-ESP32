#!/usr/bin/env python3
"""Use the Round USB diagnostic console. Screenshots remain local."""
import argparse
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["status", "snap", "home", "apps", "settings", "key1", "key2"])
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, default=Path("round-screen.rgb565"))
    parser.add_argument("--then-snap", action="store_true", help="Capture after the UI command is acknowledged")
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    port.reset_input_buffer()
    # Opening USB Serial/JTAG can reset the chip. Wait for the application,
    # rather than losing the first command during its seven-second startup.
    deadline = time.monotonic() + 20
    next_probe = 0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_probe:
            port.write(b"round status\n")
            next_probe = time.monotonic() + 1
        line = port.readline().decode("ascii", errors="replace").strip()
        if line.startswith("ROUND_STATUS"):
            if args.command == "status":
                print(line)
                return
            break
    else:
        raise TimeoutError("Round firmware did not become ready")
    port.write(f"round {args.command}\n".encode())
    end = time.monotonic() + (60 if args.command == "snap" or args.then_snap else 5)
    frame = None
    seen = set()
    info = None
    while time.monotonic() < end:
        line = port.readline().decode("ascii", errors="replace").strip()
        if line.startswith("ROUND_UI ") and args.then_snap:
            port.write(b"round snap\n")
            args.then_snap = False
            args.command = "snap"
        if line.startswith("ROUND_FRAME_BEGIN "):
            width, height, stride, size = map(int, line.split()[1:])
            if not 0 < size <= 2_000_000:
                raise ValueError("Invalid frame size")
            frame = bytearray(size)
            info = (width, height, stride)
        elif line.startswith("ROUND_DATA ") and frame is not None:
            _, offset, value = line.split()
            offset = int(offset)
            data = bytes.fromhex(value)
            if offset < 0 or offset+len(data) > len(frame):
                raise ValueError("Invalid frame chunk")
            frame[offset:offset+len(data)] = data
            seen.update(range(offset, offset+len(data)))
        elif line == "ROUND_FRAME_END":
            if frame is None or len(seen) != len(frame):
                raise ValueError("Incomplete screenshot")
            args.output.write_bytes(frame)
            args.output.with_suffix(".txt").write_text(f"width={info[0]} height={info[1]} stride={info[2]} format=RGB565LE\n")
            print(f"Saved {len(frame)} bytes: {args.output}")
            return
        elif line.startswith("ROUND_STATUS"):
            print(line)
            return
        elif line.startswith("ROUND_FRAME_ERROR"):
            raise RuntimeError(line)
    port.close()
    if args.command in ("snap", "status"):
        raise TimeoutError("No complete response from Round firmware")
    print("Command sent. Use status or snap to inspect the device.")


if __name__ == "__main__":
    main()

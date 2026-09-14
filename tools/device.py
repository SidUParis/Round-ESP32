#!/usr/bin/env python3
"""Use the Round USB diagnostic console. Screenshots remain local."""
import argparse
from pathlib import Path
import time
import re
import serial


class BufferedLines:
    """Keep partial USB records across serial read timeouts."""
    def __init__(self, port):
        self.port = port
        self.buffer = bytearray()

    def readline(self):
        if b"\n" not in self.buffer:
            self.buffer.extend(self.port.read(min(4096, max(1, self.port.in_waiting))))
        if b"\n" not in self.buffer:
            if len(self.buffer) > 65536:
                raise ValueError("Oversized USB record")
            return b""
        line, _, remainder = self.buffer.partition(b"\n")
        self.buffer = bytearray(remainder)
        return bytes(line)


def checksum32(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


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
    reader = BufferedLines(port)
    # Opening USB Serial/JTAG can reset the chip. Wait for the application,
    # rather than losing the first command during its seven-second startup.
    deadline = time.monotonic() + 20
    next_probe = 0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_probe:
            port.write(b"round status\n")
            next_probe = time.monotonic() + 1
        line = reader.readline().decode("ascii", errors="replace").strip()
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
    expected_checksum = None
    retries = 0
    while time.monotonic() < end:
        line = reader.readline().decode("ascii", errors="replace").strip()
        if line.startswith("ROUND_UI ") and args.then_snap:
            port.write(b"round snap\n")
            args.then_snap = False
            args.command = "snap"
        if line.startswith("ROUND_FRAME_BEGIN "):
            fields = line.split()
            width, height, stride, size = map(int, fields[1:5])
            expected_checksum = int(fields[5], 16) if len(fields) >= 6 else None
            if not 0 < size <= 2_000_000:
                raise ValueError("Invalid frame size")
            frame = bytearray(size)
            seen.clear()
            info = (width, height, stride)
        elif line.startswith("ROUND_DATA ") and frame is not None:
            record = re.match(r"ROUND_DATA (\d+) ([0-9a-f]+)", line)
            if not record:
                continue
            offset = int(record[1])
            value = record[2]
            count = min(256, len(frame) - offset)
            if offset < 0 or count <= 0 or len(value) < count * 2:
                continue
            value = value[:count * 2]  # Ignore log text appended after a complete record.
            data = bytes.fromhex(value)
            if offset < 0 or offset+len(data) > len(frame):
                raise ValueError("Invalid frame chunk")
            frame[offset:offset+len(data)] = data
            seen.update(range(offset, offset+len(data)))
        elif line.startswith("ROUND_FRAME_END"):
            valid = frame is not None and len(seen) == len(frame)
            if valid and expected_checksum is not None:
                valid = checksum32(frame) == expected_checksum
            if not valid:
                if retries >= 2:
                    raise ValueError("Incomplete or corrupt screenshot after three attempts")
                retries += 1
                frame = None
                port.write(b"round snap\n")
                end = time.monotonic() + 60
                continue
            args.output.write_bytes(frame)
            args.output.with_suffix(".txt").write_text(f"width={info[0]} height={info[1]} stride={info[2]} format=RGB565LE\n")
            print(f"Saved {len(frame)} bytes: {args.output}")
            return
        elif line.startswith("ROUND_STATUS"):
            if args.command == "status":
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

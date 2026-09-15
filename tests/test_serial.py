"""USB records may be split by timeouts or delivered several per read."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("device", Path(__file__).parents[1] / "tools/device.py")
device = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"serial": types.ModuleType("serial")}):
    spec.loader.exec_module(device)


class Port:
    in_waiting = 4096
    def __init__(self, chunks):
        self.chunks = iter(chunks)

    def read(self, size):
        return next(self.chunks, b"")


class SerialRecords(unittest.TestCase):
    def test_host_timezone_keeps_dst_rules(self):
        with patch.object(Path, "read_bytes", return_value=b"TZif2\0payload\nCET-1CEST,M3.5.0,M10.5.0/3\n"):
            self.assertEqual(device.host_timezone(), "CET-1CEST,M3.5.0,M10.5.0/3")

    def test_frame_checksum_known_vectors(self):
        self.assertEqual(device.checksum32(b""), 0x811C9DC5)
        self.assertEqual(device.checksum32(b"hello"), 0x4F9F2CAB)

    def test_partial_record_survives_timeout(self):
        reader = device.BufferedLines(Port([b"ROUND_DATA 0 ab", b"", b"cd\n"]))
        self.assertEqual(reader.readline(), b"")
        self.assertEqual(reader.readline(), b"")
        self.assertEqual(reader.readline(), b"ROUND_DATA 0 abcd")

    def test_multiple_records_in_one_usb_packet(self):
        reader = device.BufferedLines(Port([b"one\ntwo\n"]))
        self.assertEqual(reader.readline(), b"one")
        self.assertEqual(reader.readline(), b"two")

    def test_unbounded_record_is_rejected(self):
        reader = device.BufferedLines(Port([b"x" * 65537]))
        with self.assertRaises(ValueError):
            reader.readline()


if __name__ == "__main__":
    unittest.main()

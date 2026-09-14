"""Regression checks for destructive flash boundaries, using synthetic artifacts."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("flash", Path(__file__).parents[1] / "tools/flash.py")
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


def partition_table(nvs_offset=0x3B000):
    rows = [("nvsfactory", 0x9000, 0x32000), ("nvs", nvs_offset, 0xD2000),
            ("model", 0x110000, 0xF0000), ("factory", 0x200000, 0x800000),
            ("storage", 0xA00000, 0x600000)]
    return b"".join(struct.pack("<HBBII16sI", 0x50AA, 1, 2, offset, size,
                                name.encode().ljust(16, b"\0"), 0)
                    for name, offset, size in rows)


class FlashSafety(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.files = {}
        for offset in flash.REQUIRED_OFFSETS:
            file = self.root / f"{offset:x}.bin"
            file.write_bytes(partition_table() if offset == 0x8000 else b"test")
            self.files[hex(offset)] = file.name
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        (self.root / "flasher_args.json").write_text(json.dumps({"flash_files": self.files}))

    def test_expected_layout(self):
        self.assertEqual({entry[0] for entry in flash.plan(self.root)}, flash.REQUIRED_OFFSETS)

    def test_configuration_artifacts_are_never_written(self):
        (self.root / "nvs.bin").write_bytes(b"private configuration")
        self.files["0x3b000"] = "nvs.bin"
        self.save()
        self.assertEqual(len(flash.plan(self.root)), 5)

    def test_crossing_configuration_boundary_is_rejected(self):
        (self.root / "8000.bin").write_bytes(b"x" * 0x2000)
        with self.assertRaisesRegex(ValueError, "protected"):
            flash.plan(self.root)

    def test_changed_nvs_address_is_rejected(self):
        (self.root / "8000.bin").write_bytes(partition_table(0x40000))
        with self.assertRaisesRegex(ValueError, "nvs"):
            flash.plan(self.root)

    def test_overlapping_images_are_rejected(self):
        (self.root / "110000.bin").write_bytes(b"x" * 0xF0001)
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            flash.plan(self.root)

    def test_external_manifest_path_is_rejected(self):
        self.files["0x0"] = "../outside.bin"
        self.save()
        with self.assertRaisesRegex(ValueError, "outside"):
            flash.plan(self.root)

    def test_missing_model_cannot_be_flashed(self):
        del self.files["0x110000"]
        self.save()
        with self.assertRaisesRegex(ValueError, "layout"):
            flash.plan(self.root)


if __name__ == "__main__":
    unittest.main()

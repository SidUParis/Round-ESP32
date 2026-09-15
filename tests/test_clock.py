"""Exercise the RTC codec used by the firmware, not a Python reimplementation."""
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import tempfile
import unittest


class RtcCodec(unittest.TestCase):
    def test_utc_roundtrips_and_invalid_rtc_states(self):
        stamp = int(datetime(2026, 9, 15, 12, 34, 56, tzinfo=timezone.utc).timestamp())
        code = r'''
#include "clock_codec.hpp"
#include <cassert>
using namespace round_clock_codec;
int main() {
  std::array<uint8_t,7> r{0x56,0x34,0x12,0x15,2,0x09,0x26};
  time_t epoch=0; assert(decode(r,0,epoch)); assert(epoch==EXPECTED);
  std::array<uint8_t,7> encoded{}; assert(encode(epoch,encoded)); assert(r==encoded);
  assert(!decode(r,0x20,epoch)); assert(!decode(r,0x02,epoch));
  r[0]|=0x80;assert(!decode(r,0,epoch));r[0]=0x56;
  r[2]=0x1a;assert(!decode(r,0,epoch));r[2]=0x12;
  r[5]=0x02;r[3]=0x29;r[6]=0x24;assert(decode(r,0,epoch));
  r[6]=0x25;assert(!decode(r,0,epoch));r[3]=0x30;assert(!decode(r,0,epoch));
  r[5]=0x00;assert(!decode(r,0,epoch));
  for(time_t value : {time_t(1704067200),time_t(EXPECTED),time_t(4102444799)}) {
    assert(encode(value,encoded));assert(decode(encoded,0,epoch));assert(epoch==value);
  }
  assert(!encode(time_t(0),encoded));assert(!encode(time_t(4102444800),encoded));
}
'''.replace("EXPECTED", str(stamp))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "clock_test.cpp"
            binary = Path(directory) / "clock_test"
            source.write_text(code)
            include = Path(__file__).resolve().parents[1] / "firmware/round_shell"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(include), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()

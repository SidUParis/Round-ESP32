# Round ESP32

A quiet, circular companion interface for the Waveshare
**ESP32-S3-Touch-AMOLED-1.75** (466×466 AMOLED, 16 MiB Flash, 8 MiB PSRAM).

Round is an early native **ESP-IDF / LVGL firmware**, with a browser design
prototype alongside it. It reuses the upstream Brookesia applications instead
of replacing their hardware and AI implementations.

## Native alpha

- Circular desktop with restrained motion and a slowly changing invitation.
- Quiet connection indicator; status messages expand only when relevant.
- AI conversation presented on the desktop through the retained Xiaozhi engine.
- Original Settings, Music Player, Gravity Sphere, Spec Analyzer and other
  useful applications. The drawing application is removed from the launcher.
- PWR: return home, sleep/wake the display. BOOT: voice action; local music
  play/pause while the music app is active. Long-hold boot/power roles remain.
- Real board battery/Wi-Fi state and a USB diagnostic/screenshot console.

**Not yet implemented in native firmware:** phone BLE music remote, phone
metadata/notifications, and computer Codex/Claude Code quota providers. Those
are explicitly simulated in the browser prototype. This is not a Bluetooth
audio receiver or an Apple Watch replacement.

## Build

Install and activate **ESP-IDF v5.5.4** using the
[official instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/index.html).
The upstream source revision and target are pinned in `source.lock.json`.

```sh
git clone https://github.com/SidUParis/Round-ESP32.git
cd Round-ESP32
python tools/prepare_firmware.py
idf.py -C "$HOME/.cache/round-esp32/upstream/firmware/brookesia" \
       -B "$HOME/.cache/round-esp32/build" build
```

The project directory may have spaces, but the prepared ESP-IDF source/build
paths must not. `prepare_firmware.py` defaults to a separate cache directory.
It applies the integration patch and copies the Round component into a pinned
upstream checkout, retaining original component attribution and licenses.

## Flash with a private backup

Use only the **16 MiB ESP32-S3-Touch-AMOLED-1.75**. The layout differs from the
older factory firmware, so use the complete set of generated artifacts.

In a separate Python environment, install `esptool==5.4.0` and `pyserial==3.5`:

```sh
python tools/flash.py --build-dir "$HOME/.cache/round-esp32/build" \
                     --port /dev/ttyACM0 --dry-run
python tools/flash.py --build-dir "$HOME/.cache/round-esp32/build" \
                     --port /dev/ttyACM0
```

The script reads a fresh full backup before writing. It preserves the entire
configuration region `0x9000..0x110000`, then reads it back and compares every
byte. Backups default to `~/.local/share/round-esp32/backups` and must remain
private. Configuration byte preservation does not prove compatibility of every
older cloud activation schema; verify AI activation and connectivity afterward.

To restore an original full 16 MiB backup:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 ORIGINAL_BACKUP.bin
```

This restores the old firmware and its saved state. Do not restore someone
else's backup or publish a device dump.

## Preview and diagnostics

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory ui-preview
python tools/device.py status --port /dev/ttyACM0
python tools/device.py snap --port /dev/ttyACM0 --output round-screen.rgb565
```

Open [the local preview](http://127.0.0.1:8765/). It never records audio, pairs
devices, reads account credentials, or changes the board. It illustrates design
and future companion scenarios, not measured native frame rates.

The USB console also accepts `round home`, `round apps`, `round settings`,
`round key1` and `round key2`. The last command can activate real AI audio when
the firmware is connected and configured; it is not a harmless visual mock.

## Development

```sh
python -m unittest discover -s tests -v
node --check ui-preview/app.js
python tools/package.py --build-dir "$HOME/.cache/round-esp32/build" --output-dir dist
```

CI builds the native firmware against the pinned ESP-IDF version and tests
flash layout protection. See [architecture and limits](docs/architecture.md).

## 中文说明

这是面向 1.75 英寸圆屏的原生桌面定制项目。优先保留原有 AI 和应用，
让桌面更安静：提示按需出现，空闲文案缓慢轮换，对话直接在桌面进行。
浏览器原型中的手机遥控、设备信息和额度属于后续接入方向，当前不能当作真实硬件功能。
刷写工具先做整机备份，并验证配置区未被修改；个人备份绝不能上传到公开仓库。

## License and credit

Round additions are Apache-2.0. Upstream code and dependencies retain their
original licenses. Font subsets use the SIL Open Font License 1.1.
See [NOTICE](NOTICE), [LICENSE](LICENSE), and component notices. Built firmware
also uses vendor SDK components; this repository does not claim that every
linked vendor binary is open source. This is an independent community project.

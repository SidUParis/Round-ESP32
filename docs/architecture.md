# Architecture and scope

Round adds a native LVGL desktop over Waveshare's ESP-Brookesia application
manager. Existing applications retain their lifecycle, drivers, and audio
ownership. The desktop hides while a normal app is active and returns when it
closes. The AI application can run beneath the desktop through a small,
nonblocking state interface; its original activation and transport code stays
in control. Tapping the desktop starts this path, rather than a browser or a
second standalone AI screen.

The quiet home has a small connection indicator, transient status messages,
slowly rotating invitations, and an animated voice motif. Text rotation stops
while AI is active. Board-side animation uses ordinary LVGL objects and arcs,
not the browser prototype's blur and material effects.

PWR is sampled through the board's TCA9554 EXIO4 (active high); BOOT is GPIO0
(active low). Debounced short-release events enter the same UI queue used by
touch actions. Long holds are not redefined. PWR returns home or toggles display
sleep; BOOT operates voice, or pauses/resumes the retained local music player.

## What the native alpha does not implement

- Phone BLE media remote and Apple Media Service discovery.
- Phone metadata and notifications.
- Codex / Claude Code account usage providers and computer status bridging.
- A generic phone-like app installation system.

Those companion scenarios remain clearly labeled browser simulations. In
particular, simulated account percentages never appear in the device firmware.
The ESP32-S3 supports BLE, not Bluetooth Classic; future media control must use
an appropriate BLE protocol or an authorized companion bridge.

## Configuration preservation

The pinned partition table leaves nvsfactory at 0x9000 and nvs at 0x3B000,
matching the original factory layout. The flash tool backs up the full 16 MiB
and excludes every write in 0x9000..0x110000, then reads that region back for a
byte-for-byte comparison. This preserves the bytes; compatibility of every
older cloud activation/configuration schema still needs functional validation.
The new application, model and storage layout replaces their former regions.
The full private backup provides the data needed for restoration.

Never upload board flash dumps: they can contain network or account secrets.
Release bundles are generated only from compiler outputs. ESP-IDF and managed
vendor components keep their own licenses, including any binary SDK components.

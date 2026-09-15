# Alpha 2: time, shared status, and frame pacing

This update corrects gaps between the browser concept and the first native build.

## Causes and fixes

- The large `ROUND` text was an uninitialized-clock fallback. The desktop now
  shows a real local clock/date, or `--:--` while unsynchronized. USB can set UTC
  time and the host POSIX timezone rules; the board RTC stores UTC. The timezone
  is persisted separately in the `round_clock` NVS namespace. A valid RTC restores
  time at boot, and NTP is started when Wi-Fi becomes available.
- The native desktop and original applications used separate status bars. One
  persistent Round status component now spans the desktop, launcher, and apps.
  The original bar is hidden while its reserved content area remains intact.
- Full-page circular clipping created temporary ARGB layers. The physical
  circular panel already hides the framebuffer corners, so that software mask
  was redundant. Static orb artwork is cached in RGB565, and the covered
  Brookesia launcher is not repainted beneath the opaque Round desktop.
- A 50 ms timer capped the original motion at 20 updates/s. The corrected scene
  uses a 20 ms cadence; text transitions animate color rather than creating a
  separate full-label opacity layer. Unchanged geometry/font values are not
  repeatedly reassigned.

## Device evidence

On the same ESP32-S3 / 466×466 board, short idle measurements showed:

| Configuration | Typical average render duration | Observed steady frame rate |
| --- | --- | --- |
| Alpha 1 | about 43 ms | mostly 19–20 fps; a text transition sample near 15 fps |
| Remove redundant clip only | about 23 ms | original 20 fps timer limit retained |
| Corrected native scene | about 11–12 ms | 49–50 fps in the final transition regression run; 46 fps in an earlier text-transition sample |

Startup/loading and diagnostic screenshot transfer are excluded from these
steady windows. These are not long-duration or concurrent AI/audio benchmarks.

USB time synchronization returned success with RTC storage available. A
subsequent hardware restart restored time from RTC, and repeated diagnostic
samples showed UTC seconds advancing. Host-side regression tests cover RTC BCD
validation, oscillator-loss/STOP flags, leap days, UTC round trips and preserved
DST rules. Existing configuration-boundary and USB-record tests still run.

The RTC needs backup power to keep time while the board has no external power.
Invalid RTC state is not presented as a valid clock. NTP code is integrated but
its live network path is separate from the verified USB/RTC path.

RTC register definitions follow the
[NXP PCF85063A data sheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf).

Repeated Settings → home → launcher → home transitions passed in a single
device session after restoring only covered child objects (never hiding a screen
root, which has no layout parent). Full-display captures confirmed that the same
status component remains above the retained Settings view.

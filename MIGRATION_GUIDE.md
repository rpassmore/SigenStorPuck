# SigenStorPuck → Guition ESP32-4848S040 migration

Based on your actual `main.cpp`, `display.cpp`, `touch.cpp`, `lv_conf.h`,
`buttons.cpp`/`.h`, and `power.cpp`/`.h`. `main.cpp` needs **no changes** —
every function it calls keeps its original signature. Everything else in
`src/screens`, `src/ui`, `src/device/net.cpp`, `src/device/poller.cpp`,
`src/device/settings*.cpp`, `src/device/updater.cpp`, and `src/sim` is
untouched by this migration.

## Files in this package, and why

| File | What changed |
|---|---|
| `include/board_config.h` | Rewritten. Same macro names where the pin still exists; RGB panel + GT911 pins added; QSPI/CO5300/PMIC/RTC/IMU pins removed. |
| `src/display.cpp` | Rebuilt around `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display` instead of `Arduino_ESP32QSPI` + `Arduino_CO5300`. Your software-rotation `flush_cb` math is **kept verbatim** — it's independent of which panel is underneath. The CO5300-specific 2-pixel window-alignment `rounder_cb` is removed (not needed for a continuously-scanned framebuffer). Brightness and sleep move from the panel's native command set to backlight PWM. |
| `src/touch.cpp` | Rebuilt around a small hand-rolled GT911 I2C reader instead of SensorLib's `TouchDrvCST92xx`. Your mirroring/fine-rotation math in `indev_read_cb` is **kept verbatim**, but flagged for re-verification (see below) since it encoded a fact about the old touch sensor's physical mounting, not a general rule. |
| `src/device/power.cpp`/`.h` | Stubbed. No AXP2101 on this board. |
| `src/device/buttons.cpp`/`.h` | Stubbed. No PWR/BOOT-equivalent input on this board. |
| `include/lv_conf.h` | One real change: `LV_DPI_DEF` recalculated for a 480px/4.3in panel instead of 466px/1.75in. Comments updated to stop referencing CO5300 specifically. |
| `platformio.ini` | New `[env:guition]`. `lewisxhe/XPowersLib` and `lewisxhe/SensorLib` removed — nothing in the new backends uses them. `[env:sim]` untouched. |

## ⚠️ Two things to verify before flashing, not just believe

**1. Touch I2C clock vs. display data line.** The spec for this migration
gives touch as SDA=19, CLK=20. Every source I could cross-check for this
board's RGB pinout puts **G1 on GPIO20** — the same pin. Some batches of this
board reportedly use GPIO45 for touch SCL instead. This is a single `#define`
in `board_config.h` (`PUCK_I2C_SCL`) — check your physical board (schematic,
or continuity from the touch FPC to the SoC) before you wire it up.

**2. The ST7701 init sequence.** `display.cpp` uses a deliberately short init
table (MADCTL + Sleep Out + Display On) rather than a long register dump —
this is the specific fix a Guition ESP32-S3-4848S040 owner reported working
after a longer table caused a boot loop with a memory error, and this panel's
gamma/timing generally ships pre-set in OTP from the factory. If the screen
stays blank, shows a static pattern, or the colours/mirroring are wrong, the
`0x08` MADCTL byte in that table is the first thing to try changing — it
controls the RGB/BGR bit and both mirror bits.

## What was necessarily dropped, and why

Your `buttons.cpp` and `power.cpp` are built entirely around hardware this
board doesn't have:

- **PWR** wasn't a GPIO on the old board — it went to the AXP2101's PWRON
  pin. This board has no PMIC, so there's no PWR-equivalent signal to read at
  all, not just no pin assigned to it.
- **BOOT**'s gesture (day forward / next screen / restart) has no assigned
  equivalent input on this board either — the spec confirms no physical
  buttons.
- The **device battery indicator** (`refresh_device_battery()` in
  `main.cpp`) already degrades gracefully with no PMIC — `power.pmic_ok`
  being `false` is exactly the case it was written for — so this one needed
  no main.cpp change at all, just the stub.

That leaves four real behaviors gone, not just relocated: day-stepping,
auto-cycle toggle, one-tap next-screen, and restart/power-off. Swiping
between screens is untouched, since that's driven by the touch indev, not
by `buttons.cpp`. I've left suggested touch/web-UI homes for each in a
comment block at the top of the new `buttons.cpp` — happy to implement
whichever you want wired back up.

## Steps to integrate

1. Drop these files into your tree at the same paths.
2. Resolve the GPIO20 vs GPIO45 touch-SCL question against your physical board.
3. Merge the `[env:guition]` block into your `platformio.ini`; delete `[env:puck]`.
4. `pio run -e guition -t upload -t monitor`.
5. Check, in order: backlight comes on and the boot banner prints; the LVGL
   scene renders right-side-up (not mirrored — try the MADCTL byte if not);
   touch tracks your finger in the right place (the mirroring note above);
   rotation settings still look correct if you use them.
6. Decide on the buttons/power gap above, if you want it back.
7. `pio run -e sim` should build and behave exactly as before — it never
   touches any of the files this migration changed.

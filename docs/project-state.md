# Project State & Known Issues

**Active Branch**: `ESP32-4848S040`  
**Current Milestone**: Board migration from Waveshare AMOLED 1.75 to Guition ESP32-4848S040, plus Octopus Energy tariff screen integration.

## Recently Completed
* **Board Bring-up**: ST7701 RGB panel driver working with active-negative PCLK; GT911 touch operational on SDA=19, SCL=45.
* **Backlight PWM**: Screen dimming and full brightness control working via GPIO38 PWM.
* **Home Screen Bezel**: Import/export live power flow ring rendered around screen perimeter.
* **Tariff Rates Screen**: Full day half-hourly import/export rate chart added (`src/ui/screen_rates.cpp`, `src/device/tariff_rates.cpp`).
* **Tariff Reliability**: Handled open-ended intervals (`valid_to = null`), independent retry timers, staged buffers on network dropouts, and UTC consistency (commit `6f132f2`).
* **3D Enclosure**: Stand 3D models added in `docs/stl/`.

## Ongoing & Incomplete Work
1. **Touch Gesture Emulation for Buttons**:
   * Physical PWR/BOOT buttons do not exist on this board.
   * Day-stepping, auto-cycle toggling, and restart actions need touch-based gestures or UI controls.
2. **Home Screen Battery SoC Gauge**:
   * Move battery SoC reading into or adjacent to the battery icon for better glanceability.

## Verified vs Unverified Items
* **Verified**:
  * Firmware compiles cleanly (`guition`).
  * Simulator compiles and all 136 selftest checks pass (`sim`).
  * Direct container compilation via PlatformIO CLI.
* **Unverified in Emulator (Requires Hardware)**:
  * Inverter Modbus response with reverse function codes on live plant.
  * Physical touch calibration across all corner radii.


# Agent Context & Guidelines — SigenStorPuck

Touchscreen status monitor for Sigenergy SigenStor solar and battery systems, running on the **Guition ESP32-4848S040** board (480×480 IPS LCD, ST7701 RGB parallel, GT911 touch).

## Quick Reference Commands

Always run these commands from the repository root:

* **Build device firmware**:
  ```bash
  pio run -e guition
  ```
* **Build desktop simulator**:
  ```bash
  pio run -e sim
  ```
* **Run automated unit tests** (137 checks, headless):
  ```bash
  ./.pio/build/sim/program --selftest
  ```
* **Flash device (via serial)**:
  ```bash
  pio run -e guition -t upload --upload-port /dev/ttyUSB0
  ```
* **Monitor serial output**:
  ```bash
  pio device monitor --port /dev/ttyUSB0 --baud 115200
  ```
* **Rebuild test fixtures**:
  ```bash
  python3 tools/make_fixtures.py
  ```

> [!WARNING]
> Do not run `pio test` — Unity test suites are not configured. All regression and decoder tests are executed via `./.pio/build/sim/program --selftest`.

## Architecture & Subsystem Boundaries

* **Core Separation**:
  * **Core 1 (`src/main.cpp`)**: LVGL UI (`src/ui/`), touch handling, display refresh, local web server (`src/device/settings_server.cpp`), screen dimming schedules.
  * **Core 0 (`src/device/poller.cpp`)**: FreeRTOS worker polling inverter registers via Modbus TCP or Sigen server via HTTP, plus Octopus tariff rates and solar forecast.
* **Data Flow**:
  * Poller produces immutable `Snapshot` structs (`src/snapshot.h`).
  * Presentation layer consumes snapshots via `ui_update(snapshot)`. Screens never query network or hardware directly.
* **Modes**:
  * **Modbus Mode**: Talks directly to plant over LAN. 5 screens (Flows & Cost screens omitted). Local solar forecast model.
  * **Server Mode**: Connects to SigenStorDisplay server. 7 screens, historical day navigation.
  * **Tariff Subsystem**: Fetches Octopus Energy import/export half-hourly rates independently into `DayRates`.

Detailed architectural breakdowns are documented in [docs/architecture.md](docs/architecture.md).

## Critical Hardware & Implementation Rules

1. **Hardware Pinout**: Defined in [`src/board_config.h`](src/board_config.h). Touch SCL is **GPIO45** (GPIO20 is display G1). Backlight is PWM on **GPIO38**.
2. **Missing Hardware Stubs**: The Guition board has **no PMIC** (AXP2101) and **no physical buttons**. Keep `src/device/power.cpp` and `src/device/buttons.cpp` stubbed; do not re-introduce dependencies on `XPowersLib` or `SensorLib`.
3. **Data Representation**: Use `MaybeFloat` and `MaybeInt` with `.known` flags. Never substitute missing data with `0.0`; unconfigured/unavailable metrics render as `"--"`.
4. **Time & Day Alignment**: Keep all timestamps and slot calculations strictly in UTC. Local time offsets are applied strictly at the rendering boundary.
5. **Simulator Parity**: Core algorithms (Modbus decoders, history ring buffers, day series, solar models) must live outside `src/device/` so they compile into both `guition` firmware and `sim` native binaries.

## Further Documentation

* [Architecture & Data Flow](docs/architecture.md)
* [Development, Testing & Simulator Guide](docs/development.md)
* [Current Project State & Roadmap](docs/project-state.md)
* [Migration Notes](MIGRATION_GUIDE.md)


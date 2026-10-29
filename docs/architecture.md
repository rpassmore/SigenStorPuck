# System Architecture

The SigenStorPuck firmware runs on the **Guition ESP32-4848S040** (ESP32-S3) driving a 480×480 ST7701 display via RGB parallel interface with capacitive GT911 touch.

```
┌─────────────────────────────────────────────────────────────────┐
│                           Core 1 (UI)                           │
│                                                                 │
│   src/main.cpp (loop) ──> LVGL 8.4 ──> src/display.cpp (ST7701) │
│          │                                                      │
│          ├──> src/touch.cpp (GT911 I2C SDA=19, SCL=45)          │
│          ├──> src/device/settings_server.cpp (Port 80 Web UI)   │
│          └──> src/ui/ui.cpp ──> Screens (Power, Solar, ...)     │
└─────────────────────────────────▲───────────────────────────────┘
                                  │ ui_update(Snapshot)
┌─────────────────────────────────┴───────────────────────────────┐
│                      Core 0 (Background Poller)                 │
│                                                                 │
│   src/device/poller.cpp (FreeRTOS Task)                         │
│          ├──> DataSource::Modbus (src/device/modbus_api.cpp)    │
│          ├──> DataSource::Server (src/device/sigen_api.cpp)     │
│          ├──> Tariff API         (src/device/tariff_api.cpp)    │
│          └──> Solar Forecast     (src/solar_forecast.cpp)       │
└─────────────────────────────────────────────────────────────────┘
```

## Subsystem Breakdown

### 1. Presentation (`src/ui/`)
* Built with **LVGL 8.4.0**.
* Screens:
  * `screen_power.cpp`: Real-time power balance ring with animated particles and import/export bezel gauge.
  * `screen_battery.cpp`: Battery SoC gauge, capacity, health, temperature, and charge/discharge totals.
  * `screen_solar.cpp`: Solar output, forecast gauge ring, peak estimation, and daily generation curve.
  * `screen_load.cpp`: Total household consumption, live draw, and sub-breakdown.
  * `screen_flows.cpp`: Energy flow distribution (Server mode only).
  * `screen_cost.cpp`: Financial savings and current/next unit rate costs (Server mode only).
  * `screen_rates.cpp`: Half-hourly Octopus Energy import & export tariff slots bar chart.
  * `screen_settings.cpp`: QR code and device IP/mDNS address display.
* **Decoupling Rule**: UI screens never perform I/O or talk to network clients. They receive state through `ui_update(const Snapshot& snapshot)`.
* **Shared UI Utilities** (`src/ui/ui_common.*`, `ui_metric_pill.*`, `ui_bezel_ring.*`, `ui_stat_grid.*`, `tariff_view_helpers.*`, `chart_band.*`):
  * `ui_common` — root/group/label/caption factory functions (`puck_create_screen_root`, `puck_create_group`, `puck_create_label`, `puck_set_visible`) used by all screens.
  * `UiMetricPill` — reusable value+unit pill widget (coloured background label pair) shared across battery, solar, load, rates, and flows screens.
  * `UiBezelRing` — arc gauge component used by battery, solar, and flows screens for the outer bezel ring.
  * `UiStatGrid2x2` / `UiScreenHeader` — 2×2 stat tile grid and screen title/subtitle bar shared across all data screens.
  * `tariff_view_helpers` — price colouring (`puck_colour_for_price`) and slot-box creation shared by cost and rates screens.
  * `chart_band_create_standard` — standard chart band dimensions, smoothing, and clip-corner helper used by battery, solar, and load screens.

### 2. Core Separation & Threading
* **Core 1**: Dedicated to rendering smoothness and input responsiveness. Calls `lv_timer_handler()` every 5 ms.
* **Core 0**: Handles blocking network operations (TLS handshakes, HTTP requests, Modbus TCP transactions). Updates an internal snapshot copy protected by atomic/poller state transitions.

### 3. Data Sources & Ingestion
* **Modbus TCP Direct** (`src/device/modbus_api.cpp`):
  * Connects directly to inverter/gateway IP (port 502, slave 247).
  * Reads registers defined in `src/modbus_regs.cpp`.
  * Computes clear-sky solar forecast on-device (`src/solar_forecast.cpp`).
  * Flows and Cost screens are disabled in this mode.
* **SigenStorDisplay Server** (`src/device/sigen_api.cpp`):
  * Polls `/api/summary` from companion server using kiosk enrolment token.
  * Unlocks full 7-screen rotation and up to 7 days of historical step-back data.
* **Tariff Engine** (`src/device/tariff_rates.cpp`, `src/device/tariff_api.cpp`):
  * Polls Octopus Energy REST API for configured import and export product/tariff codes.
  * Stores 48 half-hour slots per day in UTC.
  * Operates with independent fetch and retry timers so failures in one rate feed do not block the other or inverter polling.

### 4. Hardware Abstraction
* **Display Driver** (`src/display.cpp`): Uses `Arduino_ESP32RGBPanel` and `Arduino_RGB_Display` with an ST7701 initialization sequence. Backlight brightness is controlled via LEDC PWM on GPIO38.
* **Touch Driver** (`src/touch.cpp`): Hand-rolled GT911 driver over Wire (I2C) using a STOP-then-separate-request pattern.
* **Stubs** (`src/device/power.cpp`, `src/device/buttons.cpp`): Gracefully stubbed since the Guition board lacks a PMIC and hardware pushbuttons.


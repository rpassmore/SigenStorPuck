# Development & Testing Guide

## Development Environment

The repository includes a containerized development environment configured in `Containerfile` and `.devcontainer/devcontainer.json`.
Inside the container (or host with PlatformIO and SDL2 installed), tools are ready in `PATH`.

## Build Targets

### 1. Build Firmware (ESP32-S3)
```bash
pio run -e guition
```
Build output is saved to `.pio/build/guition/firmware.bin`.

### 2. Build Desktop Simulator (Native Linux)
```bash
pio run -e sim
```
Build output is saved to `.pio/build/sim/program`.

## Automated Tests

Run the headless unit test harness:
```bash
./.pio/build/sim/program --selftest
```
This runs 136 checks covering:
* Modbus register decoding and sign conventions
* Snapshot JSON parsing and null handling
* Day baseline & day series data bucketing
* History ring buffers
* Solar clear-sky forecast algorithms
* Screen schedule window calculations

## Desktop Simulator Usage

The simulator allows rapid UI testing without flashing physical hardware.

```bash
# Launch interactive simulator
./.pio/build/sim/program

# Launch previewing 5-screen Modbus mode
./.pio/build/sim/program --modbus
```

### Keyboard Controls in Simulator
| Key | Action |
|---|---|
| `n` / `Space` | Next test fixture |
| `p` | Previous test fixture |
| `[` / `]` | Previous / next screen |
| `d` | Toggle parsed data dump overlay |
| `o` | Cycle setup/error overlays |
| `r` | Toggle screen auto-cycling |
| `-` / `=` | Step back / forward through past days |
| `,` / `.` | Adjust fine screen tilt/rotation |
| `Esc` | Quit simulator |

## Test Fixtures

Test fixtures reside in `test/fixtures/*.json` representing edge cases (normal, discharge, off-grid, unconfigured cost, null registers, stale cache).
To regenerate or update fixtures:
```bash
python3 tools/make_fixtures.py
```

## Flashing & Monitoring Hardware

Connect the ESP32-S3 via USB-C:
```bash
# Upload firmware
pio run -e guition -t upload --upload-port /dev/ttyUSB0

# Open serial monitor
pio device monitor --port /dev/ttyUSB0 --baud 115200
```


#include "power.h"

// No AXP2101 on this board — see power.h. main.cpp's
// refresh_device_battery() already handles pmic_ok == false by simply not
// showing the battery indicator (it's designed for a mains-only board too),
// so this stub needs no changes anywhere else.

void power_begin() {
  Serial.println("[power] no PMIC on this board — device battery indicator disabled");
}

PowerStatus power_status() {
  return PowerStatus{};  // pmic_ok stays false; everything else is unused downstream
}

void power_loop() {
  // Nothing to poll: there's no PMIC interrupt line, and PWR isn't a GPIO
  // this board exposes either (see buttons.cpp/h).
}

bool power_key_down() {
  return false;
}

void power_shutdown() {
  // There's no PMIC rail-cut on this board, and an ESP32-S3 without one has
  // no clean way to remove its own power — deep sleep still draws quiescent
  // current through the regulator, and isn't the same thing as "off" that a
  // PWR long-hold promised on the old board. Left as a deliberate no-op
  // rather than something that looks like it worked but didn't; see
  // MIGRATION_GUIDE.md for what to do instead if you want a software
  // "off" state (e.g. blanking the screen and ignoring input).
  Serial.println("[power] power_shutdown() requested, but this board has no PMIC to shut down");
}

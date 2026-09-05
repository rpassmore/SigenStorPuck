// Stubbed out for the Guition ESP32-4848S040: this board has no AXP2101 (or
// any other) PMIC, so there is no Puck-side battery to report and no PWRON
// pin for buttons.cpp to read. The struct and function signatures are kept
// identical to the original so main.cpp and buttons.cpp need no changes —
// see power.cpp for what each one now does.

#pragma once

#include <Arduino.h>

struct PowerStatus {
  bool pmic_ok = false;
  bool battery_present = false;
  bool usb_present = false;
  bool charging = false;
  int percent = -1;
  uint16_t millivolts = 0;
};

void power_begin();
PowerStatus power_status();
void power_loop();
bool power_key_down();
void power_shutdown();

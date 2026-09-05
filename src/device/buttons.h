// Stubbed out for the Guition ESP32-4848S040: no BOOT-style GPIO button and
// no PWR key (that was wired to the AXP2101's PWRON pin, which doesn't exist
// on this board either — see device/power.cpp). buttons_begin()/loop() are
// kept as no-ops, not deleted, so main.cpp needs no changes.
//
// This removes real functionality, not just an icon — see the table in
// buttons.cpp and MIGRATION_GUIDE.md for what each gesture used to do and
// where to hook a touch-based replacement if you want one back.

#pragma once

#include <Arduino.h>

void buttons_begin();
void buttons_loop();

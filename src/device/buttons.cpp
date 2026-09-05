#include "buttons.h"

// No physical buttons on this board. The old gesture table, kept here as a
// record of what no longer has a trigger:
//
//   PWR    press        a day back through the data           -> step_day(-1)
//          hold 2 s     auto-cycle on or off                  -> ui_set_rotate_enabled()
//          hold 5 s     power off                              -> power_shutdown()
//   BOOT   press        a day forward, no further than today   -> step_day(1)
//          hold 2 s     next screen                            -> ui_next_screen()
//          hold 5 s     restart                                 -> ESP.restart()
//
// Swiping between screens is untouched — that's read straight from the touch
// driver's LVGL indev, not from here.
//
// If you want these back, the natural touch-based homes are:
//   - day back/forward: already has a UI concept (ui_set_day_offset()) that
//     just needs a control — e.g. two small arrows on the day screen.
//   - auto-cycle toggle: a switch on the Settings screen (settings.cpp
//     already persists rotate_enabled via NVS).
//   - next screen: already reachable by swiping; the only loss is the
//     "without looking" one-tap version.
//   - restart / power off: the settings web page can carry these instead —
//     "Restart Device" as a POST endpoint next to the other settings
//     actions is a small addition, and doesn't need touch or a gesture at
//     all. "Power off" only meant something with a PMIC to cut the rails
//     (see device/power.cpp) — on this board there's nothing for it to do
//     beyond blanking the screen and ignoring input, so it's worth deciding
//     whether that's still a feature you want before wiring it back up.
//
// None of that is implemented here — it's a product decision, not a
// hardware-bringup one, so it's left for you rather than guessed at.

void buttons_begin() {
  Serial.println("[buttons] no physical buttons on this board — stubbed out");
}

void buttons_loop() {
  // Nothing to poll.
}

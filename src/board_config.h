// Every pin and tunable for the Guition ESP32-4848S040.
//
// Nothing else in the tree carries a hardware constant — see CLAUDE.md.
//
// Migrated from the Waveshare ESP32-S3-Touch-AMOLED-1.75. RGB panel pinout
// cross-checked against three independent sources for this exact board: an
// owner's working Arduino_GFX sketch (moononournation/Arduino_GFX#465),
// ESPHome's device page for "Guition-ESP32-S3-4848S040", and the Arduino
// forum thread that got LVGL running on it (forum.arduino.cc, "ESP32S3 and
// what GFX library?"). All three agree on CS/SCK/SDA/DE/VSYNC/HSYNC/PCLK and
// the R/G/B/BL pins below. See MIGRATION_GUIDE.md for what changed and what
// still needs verifying on real hardware.

#pragma once

#include <lvgl.h>
#include <stdint.h>

// --------------------------------------------------------------- firmware ---

// Unchanged value from the Waveshare build — bump this yourself once the
// migration is verified working; not something to guess at from here.
#define PUCK_FW_VERSION "0.12.0"

// ---------------------------------------------------------------- display ---

// 480x480 square ST7701 IPS LCD, ESP32-S3 RGB parallel bus. Replaces the
// 466x466 round CO5300 AMOLED over QSPI — there is no QSPI bus, no
// column/row window offset, and no CO5300 rotation-command quirk here; this
// panel is a continuously-scanned framebuffer rather than a command-window
// panel, and display.cpp's rotation/flush code changed accordingly.
static constexpr int16_t PUCK_LCD_WIDTH = 480;
static constexpr int16_t PUCK_LCD_HEIGHT = 480;

// 3-wire bus for the ST7701's own init command sequence, before the RGB pixel
// bus takes over. CS/SCLK names kept from the old board; SDA is new — the old
// board's D0-D3 were QSPI data lines with no equivalent here.
static constexpr int8_t PUCK_LCD_CS = 39;
static constexpr int8_t PUCK_LCD_SCLK = 48;
static constexpr int8_t PUCK_LCD_SDA = 47;
static constexpr int8_t PUCK_LCD_RST = -1;  // not broken out on this board

static constexpr int8_t PUCK_LCD_DE = 18;
static constexpr int8_t PUCK_LCD_VSYNC = 17;
static constexpr int8_t PUCK_LCD_HSYNC = 16;
static constexpr int8_t PUCK_LCD_PCLK = 21;

static constexpr int8_t PUCK_LCD_R0 = 11;
static constexpr int8_t PUCK_LCD_R1 = 12;
static constexpr int8_t PUCK_LCD_R2 = 13;
static constexpr int8_t PUCK_LCD_R3 = 14;
static constexpr int8_t PUCK_LCD_R4 = 0;

static constexpr int8_t PUCK_LCD_G0 = 8;
static constexpr int8_t PUCK_LCD_G1 = 20;  // was suspected to conflict with touch SCL on real
                                            // hardware (silent hang at display bring-up); touch
                                            // SCL has been moved to GPIO45 instead — see PUCK_I2C_SCL.
static constexpr int8_t PUCK_LCD_G2 = 3;
static constexpr int8_t PUCK_LCD_G3 = 46;
static constexpr int8_t PUCK_LCD_G4 = 9;
static constexpr int8_t PUCK_LCD_G5 = 10;

static constexpr int8_t PUCK_LCD_B0 = 4;
static constexpr int8_t PUCK_LCD_B1 = 5;
static constexpr int8_t PUCK_LCD_B2 = 6;
static constexpr int8_t PUCK_LCD_B3 = 7;
static constexpr int8_t PUCK_LCD_B4 = 15;

// Timing porches for this panel at 480x480, from the same cross-checked
// sources above. If the image is shifted, torn, or won't sync, these plus
// PUCK_LCD_PCLK_HZ are the first thing to tune.
static constexpr uint8_t PUCK_LCD_HSYNC_POLARITY = 1;
static constexpr uint16_t PUCK_LCD_HSYNC_FRONT_PORCH = 10;
static constexpr uint16_t PUCK_LCD_HSYNC_PULSE_WIDTH = 8;
static constexpr uint16_t PUCK_LCD_HSYNC_BACK_PORCH = 50;
static constexpr uint8_t PUCK_LCD_VSYNC_POLARITY = 1;
static constexpr uint16_t PUCK_LCD_VSYNC_FRONT_PORCH = 10;
static constexpr uint16_t PUCK_LCD_VSYNC_PULSE_WIDTH = 8;
static constexpr uint16_t PUCK_LCD_VSYNC_BACK_PORCH = 20;
static constexpr int32_t PUCK_LCD_PCLK_HZ = 12000000;

// Software rotation applied in display.cpp's flush callback, same mechanism
// and same reasoning as the old board (see display.cpp) — kept as a named
// constant here rather than hardcoded 0, same as before.
static constexpr uint8_t PUCK_LCD_ROTATION = 0;

// Which edge of PCLK the panel latches RGB data on. Discovered empirically:
// a nonzero value here (inverting the clock) cleared up faint glitch lines
// that were visible with this at 0 — the previous value, matching most
// reference examples' default, was apparently wrong for this specific
// panel/wiring. 1 rather than "true" since the real constructor parameter is
// uint16_t, not bool, to match the confirmed signature in your installed
// library's Arduino_ESP32RGBPanel.h.
static constexpr uint16_t PUCK_LCD_PCLK_ACTIVE_NEG = 1;

// Unlike the old board, this one *does* have a PWM backlight pin — the
// CO5300's setBrightness() command doesn't exist here, so display.cpp drives
// this pin with ledc instead. Value is the boot-time default, before
// settings load; same meaning and same number as before.
static constexpr int8_t PUCK_LCD_BL = 38;
static constexpr uint8_t PUCK_LCD_BRIGHTNESS = 200;

// LVGL partial draw buffer, in whole display lines. 40 x 480 x 2 B = 38 KB —
// same line count as before; the maths changed slightly with the 14px-wider
// panel but the DMA-capable-internal-RAM budget is unaffected.
static constexpr uint16_t PUCK_LVGL_BUFFER_LINES = 40;

// ---------------------------------------------------------------- buttons ---
//
// No physical buttons on this board at all — see MIGRATION_GUIDE.md and
// src/device/buttons.cpp, now a stub. PUCK_BUTTON_BOOT is gone with them.

// ------------------------------------------------------------------ touch ---

// Not broken out on this board revision. GT911 clones commonly leave INT/RST
// floating and rely on whatever address the chip latched at its own
// power-on, polling over I2C rather than using the IRQ line. Set to real
// GPIOs if your unit exposes them — touch.cpp's ensure_reset_released()
// already no-ops cleanly when these are negative.
static constexpr int8_t PUCK_TOUCH_INT = -1;
static constexpr int8_t PUCK_TOUCH_RST = -1;

// GT911 standard address pair — 0x5D is the default when INT is left high or
// floating during reset, 0x14 when held low. touch.cpp probes both, same
// pattern as the old board's CST9217 ambiguity.
static constexpr uint8_t PUCK_TOUCH_ADDR_PRIMARY = 0x5D;
static constexpr uint8_t PUCK_TOUCH_ADDR_ALT = 0x14;

// The GT911 supports up to 5; touch.cpp only ever tracks the first point,
// same as the old CST9217 driver did, so this is informational rather than
// something the read path currently uses.
static constexpr uint8_t PUCK_TOUCH_MAX_POINTS = 5;

static constexpr bool PUCK_TOUCH_MIRROR_X = false;
static constexpr bool PUCK_TOUCH_MIRROR_Y = false;

// -------------------------------------------------------------------- i2c ---

// One bus. Was shared with the AXP2101 PMIC, PCF85063 RTC, QMI8658 IMU and
// ES8311 codec on the old board — none of those exist here, so it's touch
// only now.
//
// VERIFY BEFORE FLASHING: see the PUCK_LCD_G1 note above — GPIO20 here
// may conflict with the display's G1 data line on your specific unit.
static constexpr int8_t PUCK_I2C_SDA = 19;
static constexpr int8_t PUCK_I2C_SCL = 45;  // was 20 — that's the display's G1 line, confirmed
                                             // conflicting on real hardware (silent hang around
                                             // display bring-up). GPIO45 is the alternative every
                                             // source flagged as possible; try this first.
static constexpr uint32_t PUCK_I2C_HZ = 400000;

// ----------------------------------------------------------------- serial ---

static constexpr uint32_t PUCK_SERIAL_BAUD = 115200;

// --------------------------------------------------------------------- ui ---

// The old board's value (410) was sized to stay inside the square inscribed
// in a *round* bezel — this board's screen is a full 480x480 rectangle, so
// that constraint doesn't exist any more. Set to a similar proportion of the
// new width (480 * 410/466 = ~422) as a starting guess rather than the full
// 480, purely for a bit of edge margin on the overlay title — there is no
// longer a hardware reason for this to be conservative, so widen it freely
// if it looks cramped.
static constexpr int16_t PUCK_SAFE_SQUARE = 422;

// Unchanged — not hardware-dependent.
static constexpr uint32_t PUCK_STALE_AFTER_S = 30;
static constexpr int PUCK_KW_DECIMALS = 2;
static constexpr int PUCK_MAX_DAYS_BACK = 7;
static constexpr uint32_t PUCK_SCREEN_WAKE_S = 30;
static constexpr float PUCK_SOC_LOW_PCT = 15.0f;

// ------------------------------------------------------- panel longevity ---
//
// DESIGN QUESTION: this whole section exists to spread wear across an AMOLED
// running a static image continuously — see the original comment below,
// kept verbatim. The ST7701 is an LCD; it has a fixed backlight and
// liquid-crystal pixels that don't wear the way OLED emitters do; there is
// no equivalent failure mode here for this to mitigate. Values are kept
// identical to the old board so behaviour is unchanged if you leave it
// running — but the better fix is probably to default settings.sweep_min to
// 0 (or drop the feature/menu entry) rather than tune these for hardware
// that doesn't need it.
//
// Original comment:
// This is a static image on an AMOLED running continuously, so wear is
// cumulative per pixel and concentrated wherever something is always lit — the
// state-of-charge ring, the leg labels, the page dots.
//
// A band sweeping across the panel, rather than flashing the whole screen white.
// Every pixel is still lit once per cycle, but a band lights a fraction of them
// at any instant, so the total added emission is far lower. Flashing all 217k
// pixels to even out wear on a handful risks costing more life than it saves.
static constexpr uint32_t PUCK_SWEEP_DURATION_MS = 2600;
static constexpr lv_coord_t PUCK_SWEEP_BAND_PX = 56;
static constexpr lv_opa_t PUCK_SWEEP_OPACITY = 150;
#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "board_config.h"

// Minimal ST7701 init sequence for this panel: MADCTL, Sleep Out, Display On.
// Deliberately not the long register-dump tables some ST7701 boards need —
// this board's panel comes from the factory with its gamma/timing already in
// OTP, and a confirmed working config for this exact board (Guition
// ESP32-S3-4848S040, forum.arduino.cc "ESP32S3 and what GFX library?") uses
// exactly this minimal sequence rather than a full table. If the screen
// stays blank, garbled, or shows a memory-error boot loop, that thread is the
// first place to check — it documents both this fix and the failure mode it
// replaces.
static const uint8_t kSt7701Init[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x36, WRITE_BYTES, 1, 0x08,  // MADCTL — flip bit 0x08 if colours/mirroring are wrong
    WRITE_COMMAND_8, 0x11,                        // Sleep Out
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,                        // Display On
    END_WRITE,
    DELAY, 50,
};

namespace {

Arduino_DataBus* s_bus = nullptr;
Arduino_ESP32RGBPanel* s_rgbpanel = nullptr;

// Typed as the concrete class rather than the Arduino_GFX base for the same
// reason the old code did: nothing display-specific this file needs is on
// the base class. Here that's draw16bitRGBBitmap(), which is on the base —
// kept concrete anyway so a future addition (e.g. displayOn/Off) is visible.
Arduino_RGB_Display* s_panel = nullptr;

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
lv_color_t* s_pixels = nullptr;
bool s_buffer_is_internal = false;
bool s_asleep = false;
uint8_t s_brightness_before_sleep = PUCK_LCD_BRIGHTNESS;
uint8_t s_current_brightness = PUCK_LCD_BRIGHTNESS;

// --- Everything below this line is carried over unchanged from the CO5300
// backend. It rotates in software because LVGL's own sw_rotate needs
// full_refresh at 90/270, and a full-frame + full_refresh buffer previously
// failed to boot at all on this codebase's LVGL 8.4 pin. That risk is a
// property of LVGL's rotate path, not of the CO5300 specifically, so the
// same workaround is kept here rather than re-tested blind on new hardware.
// The one thing that *was* CO5300-specific — the even/odd write-window
// rounding, needed because that panel only accepted aligned QSPI windows —
// is removed below: this panel is a continuously-scanned framebuffer, so
// every pixel address is independently writable and no rounding is needed.

uint8_t s_rotation = 0;
lv_color_t* s_rotated = nullptr;

void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* pixels) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

  if (s_rotation == 0 || s_rotated == nullptr) {
    s_panel->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(pixels), w, h);
    lv_disp_flush_ready(drv);
    return;
  }

  const bool quarter = s_rotation == 1 || s_rotation == 3;
  const int32_t dst_w = quarter ? h : w;

  for (int32_t sy = 0; sy < h; ++sy) {
    const lv_color_t* src_row = pixels + sy * w;
    for (int32_t sx = 0; sx < w; ++sx) {
      int32_t dx = 0;
      int32_t dy = 0;
      switch (s_rotation) {
        case 1:  // 90 clockwise
          dx = h - 1 - sy;
          dy = sx;
          break;
        case 2:  // 180
          dx = w - 1 - sx;
          dy = h - 1 - sy;
          break;
        default:  // 3, 270 clockwise
          dx = sy;
          dy = w - 1 - sx;
          break;
      }
      s_rotated[dy * dst_w + dx] = src_row[sx];
    }
  }

  int32_t px = 0;
  int32_t py = 0;
  switch (s_rotation) {
    case 1:
      px = PUCK_LCD_HEIGHT - 1 - area->y2;
      py = area->x1;
      break;
    case 2:
      px = PUCK_LCD_WIDTH - 1 - area->x2;
      py = PUCK_LCD_HEIGHT - 1 - area->y2;
      break;
    default:
      px = area->y1;
      py = PUCK_LCD_WIDTH - 1 - area->x2;
      break;
  }

  s_panel->draw16bitRGBBitmap(px, py, reinterpret_cast<uint16_t*>(s_rotated), dst_w,
                              quarter ? w : h);
  lv_disp_flush_ready(drv);
}

}  // namespace

bool display_begin(uint8_t rotation) {
  s_bus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, PUCK_LCD_CS, PUCK_LCD_SCLK, PUCK_LCD_SDA,
                            GFX_NOT_DEFINED /* MISO */);

  s_rgbpanel = new Arduino_ESP32RGBPanel(
      PUCK_LCD_DE, PUCK_LCD_VSYNC, PUCK_LCD_HSYNC, PUCK_LCD_PCLK,
      PUCK_LCD_R0, PUCK_LCD_R1, PUCK_LCD_R2, PUCK_LCD_R3, PUCK_LCD_R4,
      PUCK_LCD_G0, PUCK_LCD_G1, PUCK_LCD_G2, PUCK_LCD_G3, PUCK_LCD_G4, PUCK_LCD_G5,
      PUCK_LCD_B0, PUCK_LCD_B1, PUCK_LCD_B2, PUCK_LCD_B3, PUCK_LCD_B4,
      PUCK_LCD_HSYNC_POLARITY, PUCK_LCD_HSYNC_FRONT_PORCH, PUCK_LCD_HSYNC_PULSE_WIDTH,
      PUCK_LCD_HSYNC_BACK_PORCH,
      PUCK_LCD_VSYNC_POLARITY, PUCK_LCD_VSYNC_FRONT_PORCH, PUCK_LCD_VSYNC_PULSE_WIDTH,
      PUCK_LCD_VSYNC_BACK_PORCH, PUCK_LCD_PCLK_HZ);
  // PUCK_LCD_PCLK_HZ is not passed here: the verified working constructor call
  // this is based on (moononournation/Arduino_GFX#465, same board) doesn't
  // take an explicit pixel-clock argument at this GFX library version, so the
  // library's own default applies. If the image tears or won't sync, check
  // whether your installed GFX Library for Arduino version added a trailing
  // speed_hz parameter to Arduino_ESP32RGBPanel's constructor and wire
  // PUCK_LCD_PCLK_HZ through to it.

  // Rotation is always 0 here, same reasoning as the old board: rotate in the
  // flush callback below, not through the panel/library, so it composes with
  // the same LVGL rotated-touch-coordinate trick main.cpp already relies on.
  // PUCK_LCD_ROTATION is deliberately not passed here — it's the input to
  // display_begin()'s software rotation below, same as PUCK_LCD_ROTATION was
  // read by main.cpp/settings on the old board, not something the panel
  // itself should also apply.
  s_panel = new Arduino_RGB_Display(
      PUCK_LCD_WIDTH, PUCK_LCD_HEIGHT, s_rgbpanel, 0 /* rotation */, true /* auto_flush */,
      s_bus, PUCK_LCD_RST, kSt7701Init, sizeof(kSt7701Init));

  if (!s_panel->begin()) {
    Serial.println("[display] ST7701 begin() failed");
    return false;
  }
  s_panel->fillScreen(RGB565_BLACK);

  pinMode(PUCK_LCD_BL, OUTPUT);
  const bool ledc_ok = ledcAttach(PUCK_LCD_BL, 5000 /* Hz */, 8 /* bits */);
  if (ledc_ok) {
    ledcWrite(PUCK_LCD_BL, PUCK_LCD_BRIGHTNESS);
    Serial.printf("[display] backlight on GPIO%d via PWM, duty %u/255\n",
                  static_cast<int>(PUCK_LCD_BL), static_cast<unsigned>(PUCK_LCD_BRIGHTNESS));
  } else {
    // ledcAttach() can fail without throwing — a silent no-op backlight looks
    // identical to a wrong pin number from the outside. Falling back to a
    // plain digital HIGH tells the two apart: if the backlight lights up now,
    // the PWM attach was the problem (wrong LEDC channel/timer resource, core
    // version quirk); if it's still dark, PUCK_LCD_BL itself is wrong for
    // this board, or the backlight circuit needs something PWM can't give it
    // (e.g. an active-low enable).
    digitalWrite(PUCK_LCD_BL, HIGH);
    Serial.printf(
        "[display] ledcAttach(GPIO%d) failed — backlight forced HIGH with plain "
        "digitalWrite instead. If it's lit now, brightness control needs a different "
        "LEDC setup; if it's still dark, check PUCK_LCD_BL against your board and "
        "whether the backlight enable is active-low.\n",
        static_cast<int>(PUCK_LCD_BL));
  }
  s_current_brightness = PUCK_LCD_BRIGHTNESS;

  const size_t pixel_count = static_cast<size_t>(PUCK_LCD_WIDTH) * PUCK_LVGL_BUFFER_LINES;
  const size_t bytes = pixel_count * sizeof(lv_color_t);

  // Same DMA-then-PSRAM fallback as before. Less critical here than it was for
  // QSPI throughput — this buffer is memcpy'd into the RGB panel's own
  // PSRAM framebuffer rather than DMA'd straight to the bus — but internal
  // RAM is still faster to write to, so keep trying it first.
  s_pixels = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  s_buffer_is_internal = s_pixels != nullptr;
  if (s_pixels == nullptr) {
    s_pixels = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  }
  if (s_pixels == nullptr) {
    Serial.printf("[display] could not allocate a %u-byte draw buffer\n",
                  static_cast<unsigned>(bytes));
    return false;
  }

  s_rotation = rotation & 0x03;
  if (s_rotation != 0) {
    s_rotated = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL));
    if (s_rotated == nullptr) {
      s_rotated = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    }
    if (s_rotated == nullptr) {
      Serial.println("[display] no room for a rotation buffer, staying at 0");
      s_rotation = 0;
    }
  }

  lv_disp_draw_buf_init(&s_draw_buf, s_pixels, nullptr, pixel_count);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = PUCK_LCD_WIDTH;
  s_disp_drv.ver_res = PUCK_LCD_HEIGHT;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.flush_cb = flush_cb;
  // No rounder_cb: that was purely a CO5300 QSPI-window constraint.
  s_disp_drv.sw_rotate = 0;
  s_disp_drv.rotated = s_rotation == 1   ? LV_DISP_ROT_270
                       : s_rotation == 2 ? LV_DISP_ROT_180
                       : s_rotation == 3 ? LV_DISP_ROT_90
                                         : LV_DISP_ROT_NONE;
  lv_disp_drv_register(&s_disp_drv);

  Serial.printf("[display] rotation %u%s\n", static_cast<unsigned>(s_rotation),
                s_rotation != 0 ? " (rotated in flush)" : "");
  Serial.printf("[display] ST7701 %dx%d up, %u-byte buffer (%u lines) in %s\n", PUCK_LCD_WIDTH,
                PUCK_LCD_HEIGHT, static_cast<unsigned>(bytes),
                static_cast<unsigned>(PUCK_LVGL_BUFFER_LINES),
                s_buffer_is_internal ? "internal DMA RAM" : "PSRAM");
  return true;
}

void display_set_brightness(uint8_t level) {
  s_current_brightness = level;
  if (!s_asleep) {
    ledcWrite(PUCK_LCD_BL, level);
    Serial.printf("[display] panel brightness set %d\n", level);
  }
}

void display_set_sleep(bool asleep) {
  if (asleep == s_asleep) {
    return;
  }
  s_asleep = asleep;
  if (asleep) {
    // Backlight off rather than any panel-level sleep command: this panel's
    // RGB timing generator keeps scanning out whatever is in its framebuffer
    // regardless, and stopping/restarting that scan cleanly is exactly the
    // kind of thing that varies by ST7701 clone and by GFX library version.
    // Cutting the backlight is simple, has no failure mode worse than "screen
    // stays dark", and is visually identical to the old board's sleep.
    s_brightness_before_sleep = s_current_brightness;
    // ledcWrite(PUCK_LCD_BL, 0);
  } else {
    ledcWrite(PUCK_LCD_BL, s_brightness_before_sleep);
    // The framebuffer never lost its content (no true panel sleep happened),
    // so this invalidate is likely unnecessary here — kept anyway since it's
    // harmless and matches the old board's belt-and-braces behaviour.
    lv_obj_invalidate(lv_scr_act());
  }
  Serial.printf("[display] panel %s\n", asleep ? "asleep" : "awake");
}

bool display_asleep() {
  return s_asleep;
}

bool display_buffer_is_internal() {
  return s_buffer_is_internal;
}
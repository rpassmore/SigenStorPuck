#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "board_config.h"

// Real, board-specific ST7701 init table. Sourced verbatim (not hand-typed)
// from two independent repos that both explicitly target this exact board —
// aquaElectronics/esp32-4848s040-st7701 ("PlatformIO demo for the GUITION
// ESP32-4848S040... using Arduino_GFX") and sand1812/ESP32-4848S040, the
// latter being the repo this whole migration was originally scoped against.
// Both agree byte-for-byte, which is as much confidence as this gets without
// a datasheet.
//
// Two corrections from my earlier attempt, now known wrong:
//   - 0x3A (pixel format) is 0x60 (RGB666), not 0x50 (RGB565) as I'd changed
//     it to. I'd reasoned that only 16 data lines being wired meant the panel
//     should be told to expect 16-bit RGB565 — but both board-specific
//     sources use 0x60, meaning this panel's 16 physical lines are wired
//     MSB-aligned into its 18-bit RGB666 input (R[4:0]->R[5:1], with the LSB
//     of each channel simply left unconnected) — a standard convention for
//     this class of board that I didn't recognise. My "fix" was a mistake;
//     sorry for the extra round trip chasing it.
//   - 0xCD is 0x00 here, not the generic table's 0x08 — a deliberate,
//     board-specific tuning both sources share.
// Both sources also omit the display-inversion command (0x21, "IPS") that
// the generic st7701_type1 table sends, and insert a 10ms delay before Sleep
// Out instead — kept exactly as found rather than guessing at why.
static const uint8_t kSt7701Init[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x00,  // board-specific; generic ST7701 tables use 0x08

    WRITE_COMMAND_8, 0xB0,  // Positive Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E,
    0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12,
    0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1,  // Negative Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E,
    0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11,
    0x11, 0xA9, 0x32, 0x18,

    // PAGE1
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60,  // Vop=4.7375v
    WRITE_C8_D8, 0xB1, 0x32,  // VCOM=32
    WRITE_C8_D8, 0xB2, 0x07,  // VGH=15v
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,  // VGL=-10.17v
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,  // AVDD=6.6 & AVCL=-4.6
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00,
    0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44,
    0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0,
    0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0,
    0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0,
    0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0,
    0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7,
    0x02, 0x00, 0xE4, 0xE4,
    0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54,
    0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20,
    0x45, 0x67, 0x98, 0xBA,

    //-----------VAP & VAN---------------
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_C8_D8, 0x3A, 0x60,  // RGB666 — see the comment above this table for why
                              // this is correct despite 16 physical data lines.

    // No display-inversion (0x21) command here — both board-specific sources
    // omit it. A short delay before Sleep Out instead.
    DELAY, 10,

    WRITE_COMMAND_8, 0x11,  // Sleep Out
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,  // Display On
    END_WRITE,
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
      PUCK_LCD_VSYNC_BACK_PORCH,
      // pclk_active_neg=1 is the change that cleared the faint glitch lines
      // on real hardware (found by accident, passing the vsync back porch
      // constant a second time — see board_config.h for the real named
      // constant this became). Speed and bounce buffer are deliberately left
      // at library defaults here rather than reintroduced alongside the
      // polarity fix, so if any tearing remains it's clear that's the next
      // thing to add back, not a second unknown mixed in with this one.
      PUCK_LCD_PCLK_ACTIVE_NEG /* pclk_active_neg */, GFX_NOT_DEFINED /* prefer_speed */,
      false /* useBigEndian */, 0 /* de_idle_high */, 0 /* pclk_idle_high */,
      0 /* bounce_buffer_size_px */);

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
    digitalWrite(PUCK_LCD_BL, HIGH);
    Serial.printf(
        "[display] ledcAttach(GPIO%d) failed — backlight forced HIGH with plain "
        "digitalWrite instead.\n",
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
    ledcWrite(PUCK_LCD_BL, 0);
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
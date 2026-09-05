#include "touch.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>

#include "board_config.h"

// GT911, read directly over I2C rather than through a library — it's a small
// enough protocol that this matches how touch_scan_i2c() already talks to the
// bus by hand, and it avoids pulling in a new dependency for one register
// read. 16-bit register addresses, MSB first; a status byte at 0x814E with a
// "new data" flag in bit 7 and a point count in the low nibble, then up to
// five 8-byte point records from 0x8150. Standard and stable across GT911
// clones; this needs no per-vendor config-table upload to read touch points.
namespace {

constexpr uint16_t GT911_REG_STATUS = 0x814E;
constexpr uint16_t GT911_REG_POINT0 = 0x8150;
constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;

uint8_t s_address = 0;

lv_indev_drv_t s_indev_drv;
lv_point_t s_last_point = {PUCK_LCD_WIDTH / 2, PUCK_LCD_HEIGHT / 2};
bool s_pressed = false;
uint8_t s_rotation = 0;
// Cosine and sine of the *negative* fine angle, so the transform is the inverse of
// the one applied to the display. Precomputed: this runs on every touch report.
float s_fine_cos = 1.0f;
float s_fine_sin = 0.0f;
bool s_fine_active = false;

const char* i2c_device_name(uint8_t address) {
  switch (address) {
    case PUCK_TOUCH_ADDR_PRIMARY:
      return "GT911 touch";
    case PUCK_TOUCH_ADDR_ALT:
      return "GT911 touch (alternate address)";
    default:
      return "unrecognised";
  }
}

bool i2c_responds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool gt911_write_reg(uint8_t address, uint16_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Returns bytes actually read, or -1 on an I2C error.
int gt911_read_regs(uint8_t address, uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }
  const size_t got = Wire.requestFrom(static_cast<int>(address), static_cast<int>(len));
  for (size_t i = 0; i < got && i < len; ++i) {
    buf[i] = Wire.read();
  }
  return static_cast<int>(got);
}

// RST/INT are not broken out on this board (PUCK_TOUCH_RST/INT are -1), so
// there is no reset pulse to issue and no address-select strapping to drive —
// the chip is already running whatever address it latched at its own
// power-on. If your board does expose these pins, add the pulse here the way
// the old ensure_reset_released() did, driving INT to select 0x5D vs 0x14
// during the reset window per the GT911 datasheet.
//
// A plain runtime check rather than #ifdef: PUCK_TOUCH_RST is a typed
// constexpr in board_config.h (matching the rest of that file), not a
// preprocessor macro, so `#if defined(PUCK_TOUCH_RST)` would silently never
// be true regardless of its value. The compiler drops this branch entirely
// when the constant folds to false, so there's no runtime cost either way.
void ensure_reset_released() {
  if (PUCK_TOUCH_RST < 0) {
    return;
  }
  static bool done = false;
  if (done) {
    return;
  }
  pinMode(PUCK_TOUCH_RST, OUTPUT);
  digitalWrite(PUCK_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PUCK_TOUCH_RST, HIGH);
  delay(50);
  done = true;
}

bool gt911_read_point(uint16_t* x, uint16_t* y) {
  uint8_t status = 0;
  if (gt911_read_regs(s_address, GT911_REG_STATUS, &status, 1) != 1) {
    return false;
  }
  if (!(status & 0x80)) {
    // No new buffer since the last read — not an error, just nothing to report.
    return false;
  }

  const uint8_t count = status & 0x0F;
  bool have_point = false;
  if (count >= 1 && count <= 5) {
    uint8_t point[7];
    if (gt911_read_regs(s_address, GT911_REG_POINT0, point, sizeof(point)) == sizeof(point)) {
      *x = static_cast<uint16_t>(point[1] | (point[2] << 8));
      *y = static_cast<uint16_t>(point[3] | (point[4] << 8));
      have_point = true;
    }
  }

  // Must be acknowledged or the chip never posts another update.
  gt911_write_reg(s_address, GT911_REG_STATUS, 0x00);
  return have_point;
}

// Note on rotation/mirroring: the CST9217 on the old board was known, on that
// hardware, to be mounted 180 degrees round from the panel's scan order,
// which is why the old code unconditionally flipped both axes below. That
// was a fact about how that specific touch sensor was bonded to that
// specific glass — it does not necessarily hold for this board's GT911.
// VERIFY ON HARDWARE: touch a corner and see which reported coordinate
// lights up. If it's the opposite corner, the flip below is still needed;
// if it's the right corner, remove the "WIDTH - 1 -" / "HEIGHT - 1 -" and
// pass x/y straight through.
void indev_read_cb(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
  uint16_t raw_x = 0;
  uint16_t raw_y = 0;
  if (gt911_read_point(&raw_x, &raw_y)) {
    lv_coord_t x = PUCK_LCD_WIDTH - 1 - static_cast<lv_coord_t>(raw_x);
    lv_coord_t y = PUCK_LCD_HEIGHT - 1 - static_cast<lv_coord_t>(raw_y);

    // Undo the fine rotation about the centre of the panel, same as before —
    // this math has nothing to do with which touch chip is underneath.
    if (s_fine_active) {
      const float cx = PUCK_LCD_WIDTH / 2.0f;
      const float cy = PUCK_LCD_HEIGHT / 2.0f;
      const float ox = x - cx;
      const float oy = y - cy;
      x = static_cast<lv_coord_t>(lroundf(cx + ox * s_fine_cos - oy * s_fine_sin));
      y = static_cast<lv_coord_t>(lroundf(cy + ox * s_fine_sin + oy * s_fine_cos));
    }

    s_last_point.x = x;
    s_last_point.y = y;
    s_pressed = true;
  } else {
    s_pressed = false;
  }

  data->point = s_last_point;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

uint8_t touch_scan_i2c() {
  ensure_reset_released();

  uint8_t found = 0;
  Serial.printf("[i2c] scanning SDA=%d SCL=%d\n", PUCK_I2C_SDA, PUCK_I2C_SCL);
  for (uint8_t address = 0x08; address < 0x78; ++address) {
    if (i2c_responds(address)) {
      Serial.printf("[i2c]   0x%02X  %s\n", address, i2c_device_name(address));
      ++found;
    }
  }
  if (found == 0) {
    Serial.println("[i2c]   nothing responded — check the bus pins");
  }
  return found;
}

bool touch_begin() {
  ensure_reset_released();

  const uint8_t candidates[] = {PUCK_TOUCH_ADDR_PRIMARY, PUCK_TOUCH_ADDR_ALT};
  for (uint8_t address : candidates) {
    if (!i2c_responds(address)) {
      continue;
    }
    uint8_t product_id[4] = {};
    if (gt911_read_regs(address, GT911_REG_PRODUCT_ID, product_id, sizeof(product_id)) !=
        sizeof(product_id)) {
      Serial.printf("[touch] 0x%02X answered the bus but would not answer a register read\n",
                    address);
      continue;
    }
    s_address = address;
    Serial.printf("[touch] GT911 (id \"%c%c%c%c\") at 0x%02X, rotation %u\n", product_id[0],
                  product_id[1], product_id[2], product_id[3], s_address,
                  static_cast<unsigned>(s_rotation));
    break;
  }

  if (s_address == 0) {
    Serial.println("[touch] no GT911 found at 0x5D or 0x14 — display only");
    return false;
  }

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = indev_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  return true;
}

uint8_t touch_address() {
  return s_address;
}

bool touch_pressed(lv_point_t* point) {
  if (point != nullptr) {
    *point = s_last_point;
  }
  return s_pressed;
}

void touch_set_orientation(uint8_t rotation) {
  s_rotation = rotation & 0x03;
}

void touch_set_fine_rotation(int16_t tenths_of_a_degree) {
  s_fine_active = tenths_of_a_degree != 0;
  const float radians = -static_cast<float>(tenths_of_a_degree) / 10.0f * 3.14159265f / 180.0f;
  s_fine_cos = cosf(radians);
  s_fine_sin = sinf(radians);
}
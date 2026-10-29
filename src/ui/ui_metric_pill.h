// Live metric pill capsule component used across telemetry screens.

#pragma once

#include <lvgl.h>
#include <stdint.h>

class UiMetricPill {
 public:
  void create(lv_obj_t* parent, uint32_t colour, lv_coord_t y_offset = -26,
              const lv_font_t* font = nullptr);
  void set_text(const char* text);
  void set_active(bool active);
  void set_colour(uint32_t colour);
  void set_visible(bool visible);

  lv_obj_t* container() const { return container_; }
  lv_obj_t* label() const { return label_; }

 private:
  lv_obj_t* container_ = nullptr;
  lv_obj_t* label_ = nullptr;
  uint32_t base_colour_ = 0;
};

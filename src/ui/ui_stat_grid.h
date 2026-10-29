// 2x2 secondary telemetry stat grid and screen header components.

#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "theme.h"

struct UiStatCell {
  lv_obj_t* caption = nullptr;
  lv_obj_t* value = nullptr;

  void set(const char* text);
  void set_or_dash(const char* text, bool valid);
};

class UiStatGrid2x2 {
 public:
  static constexpr lv_coord_t COLUMN_X = 88;
  static constexpr lv_coord_t ROW_ONE_LABEL_Y = 44;
  static constexpr lv_coord_t ROW_ONE_VALUE_Y = 70;
  static constexpr lv_coord_t ROW_TWO_LABEL_Y = 116;
  static constexpr lv_coord_t ROW_TWO_VALUE_Y = 142;

  void create(lv_obj_t* parent,
              const char* tl_caption, const char* tr_caption,
              const char* bl_caption, const char* br_caption);
  void set_visible(bool visible);

  UiStatCell top_left;
  UiStatCell top_right;
  UiStatCell bottom_left;
  UiStatCell bottom_right;
};

struct UiScreenHeader {
  lv_obj_t* title = nullptr;
  lv_obj_t* hero = nullptr;
  lv_obj_t* caption = nullptr;

  void create(lv_obj_t* parent, const char* title_text, const char* caption_text = nullptr,
              uint32_t hero_colour = PUCK_COLOUR_TEXT, lv_coord_t y_title = -166);
  void set_hero(const char* text);
  void set_caption(const char* text);
};

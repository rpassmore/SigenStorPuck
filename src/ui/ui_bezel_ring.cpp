#include "ui_bezel_ring.h"

#include "theme.h"
#include "ui_common.h"

void UiBezelRing::create(lv_obj_t* parent, uint32_t indicator_colour, bool hidden) {
  arc_ = lv_arc_create(parent);
  lv_obj_set_size(arc_, PUCK_RING_DIAMETER, PUCK_RING_DIAMETER);
  lv_obj_center(arc_);
  lv_arc_set_rotation(arc_, 270);
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_range(arc_, 0, 100);
  lv_arc_set_value(arc_, 0);
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc_, PUCK_RING_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, PUCK_RING_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_, lv_color_hex(indicator_colour), LV_PART_INDICATOR);
  if (hidden) {
    lv_obj_add_flag(arc_, LV_OBJ_FLAG_HIDDEN);
  }
}

void UiBezelRing::set_value(int32_t percent) {
  if (arc_ == nullptr) return;
  lv_arc_set_value(arc_, percent);
}

void UiBezelRing::set_value(const MaybeInt& val) {
  if (arc_ == nullptr) return;
  if (val.known) {
    lv_arc_set_value(arc_, val.value);
    set_visible(true);
  } else {
    set_visible(false);
  }
}

void UiBezelRing::set_visible(bool visible) {
  puck_set_visible(arc_, visible);
}

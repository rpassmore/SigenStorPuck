#include "ui_metric_pill.h"

#include "theme.h"
#include "ui_common.h"

void UiMetricPill::create(lv_obj_t* parent, uint32_t colour, lv_coord_t y_offset,
                          const lv_font_t* font) {
  base_colour_ = colour;
  if (font == nullptr) {
    font = PUCK_FONT_LARGE;
  }

  container_ = lv_obj_create(parent);
  lv_obj_remove_style_all(container_);
  lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(container_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(container_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(container_, 18, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(container_, 7, LV_PART_MAIN);
  lv_obj_set_style_bg_color(container_, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container_, LV_OPA_20, LV_PART_MAIN);
  lv_obj_align(container_, LV_ALIGN_CENTER, 0, y_offset);

  label_ = lv_label_create(container_);
  lv_obj_set_style_text_font(label_, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label_, lv_color_hex(colour), LV_PART_MAIN);
  lv_label_set_text(label_, "--");
  lv_obj_center(label_);
}

void UiMetricPill::set_text(const char* text) {
  if (label_ == nullptr) return;
  lv_label_set_text(label_, text);
}

void UiMetricPill::set_active(bool active) {
  if (container_ == nullptr || label_ == nullptr) return;
  const uint32_t colour = active ? base_colour_ : PUCK_COLOUR_MUTED;
  lv_obj_set_style_text_color(label_, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_color(container_, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container_, active ? LV_OPA_20 : LV_OPA_10, LV_PART_MAIN);
}

void UiMetricPill::set_colour(uint32_t colour) {
  base_colour_ = colour;
  if (container_ == nullptr || label_ == nullptr) return;
  lv_obj_set_style_text_color(label_, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_color(container_, lv_color_hex(colour), LV_PART_MAIN);
}

void UiMetricPill::set_visible(bool visible) {
  puck_set_visible(container_, visible);
}

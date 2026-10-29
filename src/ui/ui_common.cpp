#include "ui_common.h"

#include "board_config.h"
#include "theme.h"

lv_obj_t* puck_create_screen_root(lv_obj_t* parent) {
  lv_obj_t* root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, PUCK_LCD_WIDTH, PUCK_LCD_HEIGHT);
  lv_obj_set_style_bg_color(root, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  return root;
}

lv_obj_t* puck_create_group(lv_obj_t* parent) {
  lv_obj_t* group = lv_obj_create(parent);
  lv_obj_remove_style_all(group);
  lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  return group;
}

lv_obj_t* puck_create_label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour,
                            lv_coord_t x, lv_coord_t y, lv_align_t align) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colour), LV_PART_MAIN);
  if (align != LV_ALIGN_DEFAULT) {
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, align, x, y);
  }
  return label;
}

lv_obj_t* puck_create_caption(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y,
                              uint8_t letter_space) {
  lv_obj_t* label = puck_create_label(parent, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, x, y, LV_ALIGN_CENTER);
  lv_obj_set_style_text_letter_space(label, letter_space, LV_PART_MAIN);
  lv_label_set_text(label, text);
  return label;
}

void puck_set_visible(lv_obj_t* obj, bool visible) {
  if (obj == nullptr) return;
  if (visible) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void puck_set_visible(lv_obj_t* const objs[], size_t count, bool visible) {
  for (size_t i = 0; i < count; ++i) {
    puck_set_visible(objs[i], visible);
  }
}

void puck_set_label_or_dash(lv_obj_t* label, const char* text, bool valid) {
  if (label == nullptr) return;
  lv_label_set_text(label, valid ? text : "--");
}

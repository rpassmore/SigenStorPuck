#include "ui_stat_grid.h"

#include "ui_common.h"

void UiStatCell::set(const char* text) {
  if (value == nullptr) return;
  lv_label_set_text(value, text);
}

void UiStatCell::set_or_dash(const char* text, bool valid) {
  if (value == nullptr) return;
  puck_set_label_or_dash(value, text, valid);
}

void UiStatGrid2x2::create(lv_obj_t* parent,
                           const char* tl_caption, const char* tr_caption,
                           const char* bl_caption, const char* br_caption) {
  top_left.caption = puck_create_caption(parent, tl_caption, -COLUMN_X, ROW_ONE_LABEL_Y);
  top_left.value = puck_create_label(parent, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, -COLUMN_X, ROW_ONE_VALUE_Y);
  lv_label_set_text(top_left.value, "--");

  top_right.caption = puck_create_caption(parent, tr_caption, COLUMN_X, ROW_ONE_LABEL_Y);
  top_right.value = puck_create_label(parent, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, COLUMN_X, ROW_ONE_VALUE_Y);
  lv_label_set_text(top_right.value, "--");

  bottom_left.caption = puck_create_caption(parent, bl_caption, -COLUMN_X, ROW_TWO_LABEL_Y);
  bottom_left.value = puck_create_label(parent, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, -COLUMN_X, ROW_TWO_VALUE_Y);
  lv_label_set_text(bottom_left.value, "--");

  bottom_right.caption = puck_create_caption(parent, br_caption, COLUMN_X, ROW_TWO_LABEL_Y);
  bottom_right.value = puck_create_label(parent, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, COLUMN_X, ROW_TWO_VALUE_Y);
  lv_label_set_text(bottom_right.value, "--");
}

void UiStatGrid2x2::set_visible(bool visible) {
  lv_obj_t* const objs[] = {
      top_left.caption,     top_left.value,
      top_right.caption,    top_right.value,
      bottom_left.caption,  bottom_left.value,
      bottom_right.caption, bottom_right.value,
  };
  puck_set_visible(objs, sizeof(objs) / sizeof(objs[0]), visible);
}

void UiScreenHeader::create(lv_obj_t* parent, const char* title_text, const char* caption_text,
                            uint32_t hero_colour, lv_coord_t y_title) {
  title = puck_create_label(parent, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, y_title);
  lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
  lv_label_set_text(title, title_text);

  hero = puck_create_label(parent, PUCK_FONT_HERO, hero_colour, 0, -114);
  lv_label_set_text(hero, "--");

  if (caption_text != nullptr) {
    caption = puck_create_label(parent, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -70);
    lv_label_set_text(caption, caption_text);
  }
}

void UiScreenHeader::set_hero(const char* text) {
  if (hero == nullptr) return;
  lv_label_set_text(hero, text);
}

void UiScreenHeader::set_caption(const char* text) {
  if (caption == nullptr) return;
  lv_label_set_text(caption, text);
}

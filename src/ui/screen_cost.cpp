#include "screen_cost.h"

#include <stdio.h>

#include "board_config.h"
#include "format.h"
#include "tariff_view_helpers.h"
#include "theme.h"
#include "ui_common.h"

namespace {

constexpr lv_coord_t ROW_WIDTH = 250;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_saving = nullptr;
lv_obj_t* s_saving_caption = nullptr;
lv_obj_t* s_rate_now = nullptr;
lv_obj_t* s_slots_box = nullptr;
lv_obj_t* s_slot_when[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_slot_price[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_slot_rows[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_unconfigured = nullptr;

}  // namespace

lv_obj_t* screen_cost_create(lv_obj_t* parent) {
  s_root = puck_create_screen_root(parent);

  lv_obj_t* title = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -138);
  lv_label_set_text(title, "COST");

  // The pound sign here is the whole reason for the custom font subset: LVGL's
  // built-in Montserrat stops at ASCII.
  s_saving = puck_create_label(s_root, PUCK_FONT_HERO, PUCK_COLOUR_BATTERY, 0, -90);
  lv_label_set_text(s_saving, "--");

  s_saving_caption = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -52);
  lv_label_set_text(s_saving_caption, "saved today");

  s_rate_now = puck_create_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, 0, -12);
  lv_label_set_text(s_rate_now, "--");

  s_slots_box = puck_create_tariff_slots_box(s_root, 90);

  for (size_t i = 0; i < SNAPSHOT_MAX_TARIFF_SLOTS; ++i) {
    lv_obj_t* row = puck_create_group(s_slots_box);
    lv_obj_set_size(row, ROW_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s_slot_rows[i] = row;

    s_slot_when[i] = puck_create_label(row, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, 0, LV_ALIGN_DEFAULT);
    lv_label_set_text(s_slot_when[i], "");

    s_slot_price[i] = puck_create_label(row, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, 0, 0, LV_ALIGN_DEFAULT);
    lv_label_set_text(s_slot_price[i], "");
  }

  s_unconfigured = puck_create_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_MUTED, 0, 0);
  lv_label_set_text(s_unconfigured, "no tariff set");
  puck_set_visible(s_unconfigured, false);

  return s_root;
}

void screen_cost_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  const bool configured = snapshot.valid && snapshot.cost.configured;

  if (!configured) {
    puck_set_visible(s_saving, false);
    puck_set_visible(s_saving_caption, false);
    lv_label_set_text(s_rate_now, "");
    puck_set_visible(s_slots_box, false);
    puck_set_visible(s_unconfigured, true);
    lv_label_set_text(s_unconfigured, snapshot.valid ? "no tariff set" : "offline");
    return;
  }
  puck_set_visible(s_saving, true);
  puck_set_visible(s_saving_caption, true);
  puck_set_visible(s_slots_box, true);
  puck_set_visible(s_unconfigured, false);

  const Snapshot::Cost& cost = snapshot.cost;
  char text[32];
  char scratch[16];

  if (cost.saving_gbp.known) {
    snprintf(text, sizeof(text), "£%.2f", cost.saving_gbp.value);
    lv_label_set_text(s_saving, text);
    lv_obj_set_style_text_color(
        s_saving,
        lv_color_hex(cost.saving_gbp.value < 0.0f ? PUCK_COLOUR_WARN : PUCK_COLOUR_BATTERY),
        LV_PART_MAIN);
  } else {
    lv_label_set_text(s_saving, "--");
  }

  if (cost.rate_p.known) {
    puck_format_magnitude(cost.rate_p, 1, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "%sp per kWh now", scratch);
    lv_label_set_text(s_rate_now, text);
  } else {
    lv_label_set_text(s_rate_now, "rate unknown");
  }

  for (size_t i = 0; i < SNAPSHOT_MAX_TARIFF_SLOTS; ++i) {
    if (i >= cost.next_count) {
      puck_set_visible(s_slot_rows[i], false);
      continue;
    }
    puck_set_visible(s_slot_rows[i], true);

    const int32_t ahead = static_cast<int32_t>(cost.next[i].from) - static_cast<int32_t>(snapshot.ts);
    puck_format_offset(ahead, scratch, sizeof(scratch));
    lv_label_set_text(s_slot_when[i], scratch);

    snprintf(text, sizeof(text), "%.1fp", cost.next[i].pence);
    lv_label_set_text(s_slot_price[i], text);
    lv_obj_set_style_text_color(s_slot_price[i],
                                lv_color_hex(puck_colour_for_price(cost.next[i].pence, cost.rate_p)),
                                LV_PART_MAIN);
  }
}

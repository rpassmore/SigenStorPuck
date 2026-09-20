#include "screen_cost.h"

#include <stdio.h>
#include <time.h>

#include "board_config.h"
#include "format.h"
#include "theme.h"

namespace {

constexpr lv_coord_t ROW_WIDTH = 250;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_rate_imp_now = nullptr;
lv_obj_t* s_pill = nullptr;
lv_obj_t* s_rate_exp_now = nullptr;
lv_obj_t* s_caption = nullptr;
lv_obj_t* s_slots_box = nullptr;
lv_obj_t* s_slot_when[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_slot_price_imp[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_slot_price_exp[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_slot_rows[SNAPSHOT_MAX_TARIFF_SLOTS] = {};
lv_obj_t* s_unconfigured = nullptr;

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour, lv_coord_t x,
                     lv_coord_t y) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_CENTER, x, y);
  return label;
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colour), LV_PART_MAIN);
  return label;
}

lv_obj_t* make_group(lv_obj_t* parent) {
  lv_obj_t* group = lv_obj_create(parent);
  lv_obj_remove_style_all(group);
  lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  return group;
}

// Cheap or dear relative to what you are paying right now, not to a threshold
// somebody has to configure. "Cheaper than now" is the question you actually ask
// when deciding whether to wait before running something.
uint32_t colour_for_price(float pence, const MaybeFloat& rate_now) {
  if (!rate_now.known) {
    return PUCK_COLOUR_TEXT;
  }
  // A hair either side of the current rate is the same rate, not a change.
  const float margin = 0.2f;
  if (pence < rate_now.value - margin) {
    return PUCK_COLOUR_BATTERY;  // green: cheaper than now
  }
  if (pence > rate_now.value + margin) {
    return PUCK_COLOUR_WARN;  // amber: dearer than now
  }
  return PUCK_COLOUR_MUTED;
}

}  // namespace

lv_obj_t* screen_cost_create(lv_obj_t* parent) {
  s_root = make_group(parent);
  lv_obj_set_size(s_root, PUCK_LCD_WIDTH, PUCK_LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t* title = make_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
  lv_label_set_text(title, "COST");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -170);

  // Import tariff rate
  s_rate_imp_now = make_label(s_root, PUCK_FONT_HERO, PUCK_COLOUR_TEXT, 0, -124);
  lv_label_set_text(s_rate_imp_now, "--");

  // The export tariff rate, in the same pill screen 2 uses for the same job.
  s_pill = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_pill);
  lv_obj_clear_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(s_pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_pill, 18, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_pill, 7, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_pill, lv_color_hex(PUCK_COLOUR_EXPORT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_pill, LV_OPA_20, LV_PART_MAIN);
  lv_obj_align(s_pill, LV_ALIGN_CENTER, 0, -64);

  s_rate_exp_now = lv_label_create(s_pill);
  lv_obj_set_style_text_font(s_rate_exp_now, PUCK_FONT_LARGE, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_rate_exp_now, lv_color_hex(PUCK_COLOUR_EXPORT), LV_PART_MAIN);
  lv_label_set_text(s_rate_exp_now, "--");
  lv_obj_center(s_rate_exp_now);

  s_caption = make_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -26);
  lv_label_set_text(s_caption, "now");

  s_slots_box = make_group(s_root);
  lv_obj_set_size(s_slots_box, ROW_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_slots_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_slots_box, 6, LV_PART_MAIN);
  lv_obj_align(s_slots_box, LV_ALIGN_CENTER, 0, 90);

  for (size_t i = 0; i < SNAPSHOT_MAX_TARIFF_SLOTS; ++i) {
    lv_obj_t* row = make_group(s_slots_box);
    lv_obj_set_size(row, ROW_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s_slot_rows[i] = row;

    s_slot_when[i] = make_label(row, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
    lv_label_set_text(s_slot_when[i], "");

    s_slot_price_imp[i] = make_label(row, PUCK_FONT_BODY, PUCK_COLOUR_TEXT);
    lv_label_set_text(s_slot_price_imp[i], "");

    s_slot_price_exp[i] = make_label(row, PUCK_FONT_BODY, PUCK_COLOUR_EXPORT);
    lv_label_set_text(s_slot_price_exp[i], "");
  }

  s_unconfigured = make_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_MUTED);
  lv_label_set_text(s_unconfigured, "no tariff set");
  lv_obj_set_style_text_align(s_unconfigured, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(s_unconfigured, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);

  return s_root;
}

void screen_cost_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  const DayTariffRates& rates = snapshot.day_rates;
  const bool configured = snapshot.valid && (rates.import_valid || snapshot.cost.configured);

  // The server says configured:false rather than omitting the block, so there is
  // a real difference between "no tariff set up" and "we could not reach it".
  if (!configured) {
    lv_label_set_text(s_rate_imp_now, "");
    lv_obj_add_flag(s_slots_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_unconfigured, snapshot.valid ? "no tariff set" : "offline");
    return;
  }
  lv_obj_clear_flag(s_slots_box, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);

  const Snapshot::Cost& cost = snapshot.cost;
  char text[32];
  char scratch[16];

  uint8_t slot_idx = 0;
  time_t now = snapshot.ts != 0 ? static_cast<time_t>(snapshot.ts) : time(nullptr);
  struct tm tm_now = {};
  const bool time_valid = (localtime_r(&now, &tm_now) != nullptr);
  if (time_valid) {
    uint8_t calculated = (tm_now.tm_hour * 2) + (tm_now.tm_min >= 30 ? 1 : 0);
    slot_idx = calculated < 48 ? calculated : 47;
  }

  MaybeFloat rate_imp_now;
  if (time_valid && rates.import_valid && rates.import_slots[slot_idx].valid) {
    rate_imp_now.known = true;
    rate_imp_now.value = rates.import_slots[slot_idx].pence;
    puck_format_magnitude(rate_imp_now, 1, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "Buy %sp kWh", scratch);
    lv_label_set_text(s_rate_imp_now, text);
  } else {
    lv_label_set_text(s_rate_imp_now, "Import rate unknown");
  }

  MaybeFloat rate_exp_now;
  if (time_valid && rates.export_valid && rates.export_slots[slot_idx].valid) {
    rate_exp_now.known = true;
    rate_exp_now.value = rates.export_slots[slot_idx].pence;
    puck_format_magnitude(rate_exp_now, 1, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "Sell %sp kWh", scratch);
    lv_label_set_text(s_rate_exp_now, text);
  } else {
    lv_label_set_text(s_rate_exp_now, "Export rate unknown");
  }


  const int32_t seconds_since_midnight =
      time_valid ? (tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec) : 0;

  for (size_t i = 0; i < SNAPSHOT_MAX_TARIFF_SLOTS; ++i) {
    const size_t next_idx = slot_idx + 1 + i;
    bool has_slot = false;
    float slot_pence = 0.0f;
    int32_t ahead = 0;

    if (time_valid && rates.import_valid && next_idx < 48 && rates.import_slots[next_idx].valid) {
      has_slot = true;
      slot_pence = rates.import_slots[next_idx].pence;
      ahead = static_cast<int32_t>(next_idx * 1800) - seconds_since_midnight;
    } else if (i < cost.next_count) {
      has_slot = true;
      slot_pence = cost.next[i].pence;
      ahead = static_cast<int32_t>(cost.next[i].from) - static_cast<int32_t>(snapshot.ts);
    }

    if (!has_slot) {
      lv_obj_add_flag(s_slot_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(s_slot_rows[i], LV_OBJ_FLAG_HIDDEN);

    puck_format_offset(ahead, scratch, sizeof(scratch));
    lv_label_set_text(s_slot_when[i], scratch);

    snprintf(text, sizeof(text), "%.1fp", slot_pence);
    lv_label_set_text(s_slot_price_imp[i], text);
    lv_obj_set_style_text_color(
        s_slot_price_imp[i],
        lv_color_hex(colour_for_price(slot_pence, rate_imp_now)),
        LV_PART_MAIN);
    
    snprintf(text, sizeof(text), "%.1fp", slot_pence);
    lv_label_set_text(s_slot_price_exp[i], text);
    lv_obj_set_style_text_color(
        s_slot_price_exp[i], lv_color_hex(PUCK_COLOUR_EXPORT),
        LV_PART_MAIN);
  }
}

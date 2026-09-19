#include "screen_rates.h"

#include <stdio.h>
#include <time.h>
#include <algorithm>

#include "board_config.h"
#include "format.h"
#include "theme.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_rates_header = nullptr;
lv_obj_t* s_imp_label = nullptr;
lv_obj_t* s_exp_label = nullptr;
lv_obj_t* s_chart = nullptr;
lv_chart_series_t* s_series_import = nullptr;
lv_chart_series_t* s_series_export = nullptr;
lv_chart_cursor_t* s_cursor = nullptr;
lv_obj_t* s_unconfigured = nullptr;
lv_obj_t* s_time_legend = nullptr;

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

}  // namespace

lv_obj_t* screen_rates_create(lv_obj_t* parent) {
  s_root = make_group(parent);
  lv_obj_set_size(s_root, PUCK_LCD_WIDTH, PUCK_LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);

  s_title = make_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_label_set_text(s_title, "DAY TARIFF RATES");
  lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -170);

  // Rates Header Row: Import & Export text
  s_rates_header = make_group(s_root);
  lv_obj_set_size(s_rates_header, 320, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_rates_header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_rates_header, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_align(s_rates_header, LV_ALIGN_CENTER, 0, -125);

  s_imp_label = make_label(s_rates_header, PUCK_FONT_BODY, PUCK_COLOUR_BATTERY);
  lv_label_set_text(s_imp_label, "Imp: --p");

  s_exp_label = make_label(s_rates_header, PUCK_FONT_BODY, PUCK_COLOUR_WARN);
  lv_label_set_text(s_exp_label, "Exp: --p");

  // Chart Component
  s_chart = lv_chart_create(s_root);
  lv_obj_set_size(s_chart, 340, 180);
  lv_obj_align(s_chart, LV_ALIGN_CENTER, 0, 15);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, 48);

  // Background and grid lines
  lv_obj_set_style_bg_color(s_chart, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_chart, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(s_chart, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
  lv_chart_set_div_line_count(s_chart, 3, 4);

  // Add Import & Export series
  s_series_import =
      lv_chart_add_series(s_chart, lv_color_hex(PUCK_COLOUR_BATTERY), LV_CHART_AXIS_PRIMARY_Y);
  s_series_export =
      lv_chart_add_series(s_chart, lv_color_hex(PUCK_COLOUR_WARN), LV_CHART_AXIS_PRIMARY_Y);

  // Vertical line cursor for active slot
  s_cursor = lv_chart_add_cursor(s_chart, lv_color_hex(PUCK_COLOUR_TEXT), LV_DIR_VER);

  // Time Legend (00:00, 12:00, 24:00)
  s_time_legend = make_group(s_root);
  lv_obj_set_size(s_time_legend, 340, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_time_legend, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_time_legend, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_align(s_time_legend, LV_ALIGN_CENTER, 0, 120);

  lv_obj_t* t0 = make_label(s_time_legend, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_label_set_text(t0, "00:00");
  lv_obj_t* t12 = make_label(s_time_legend, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_label_set_text(t12, "12:00");
  lv_obj_t* t24 = make_label(s_time_legend, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_label_set_text(t24, "24:00");

  // Unconfigured state label
  s_unconfigured = make_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_MUTED);
  lv_label_set_text(s_unconfigured, "No Tariff Rates");
  lv_obj_set_style_text_align(s_unconfigured, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(s_unconfigured, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);

  return s_root;
}

void screen_rates_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  const DayTariffRates& rates = snapshot.day_rates;
  bool has_data = rates.import_valid || rates.export_valid;

  if (!has_data) {
    lv_obj_add_flag(s_rates_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_time_legend, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(s_rates_header, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_time_legend, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);

  // Position vertical line cursor at active slot index based on timestamp
  uint8_t slot_idx = 0;
  time_t now = snapshot.ts != 0 ? static_cast<time_t>(snapshot.ts) : time(nullptr);
  struct tm tm_now = {};
  if (localtime_r(&now, &tm_now) != nullptr) {
    uint8_t calculated = (tm_now.tm_hour * 2) + (tm_now.tm_min >= 30 ? 1 : 0);
    slot_idx = calculated < 48 ? calculated : 47;
  }

  // Update current textual rates
  float cur_imp = 0.0f, cur_exp = 0.0f;
  bool imp_known = rates.import_valid && rates.import_slots[slot_idx].valid;
  bool exp_known = rates.export_valid && rates.export_slots[slot_idx].valid;
  if (imp_known) {
    cur_imp = rates.import_slots[slot_idx].pence;
  }
  if (exp_known) {
    cur_exp = rates.export_slots[slot_idx].pence;
  }

  char buf[32];
  if (rates.import_valid && imp_known) {
    snprintf(buf, sizeof(buf), "Imp: %.1fp", cur_imp);
    lv_label_set_text(s_imp_label, buf);
    lv_obj_clear_flag(s_imp_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_imp_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (rates.export_valid && exp_known) {
    snprintf(buf, sizeof(buf), "Exp: %.1fp", cur_exp);
    lv_label_set_text(s_exp_label, buf);
    lv_obj_clear_flag(s_exp_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_exp_label, LV_OBJ_FLAG_HIDDEN);
  }

  // Calculate Y min and Y max range for chart scaling
  float min_val = 0.0f;
  float max_val = 20.0f;  // default scale ceiling

  for (int i = 0; i < 48; ++i) {
    if (rates.import_valid && rates.import_slots[i].valid) {
      min_val = std::min(min_val, rates.import_slots[i].pence);
      max_val = std::max(max_val, rates.import_slots[i].pence);
    }
    if (rates.export_valid && rates.export_slots[i].valid) {
      min_val = std::min(min_val, rates.export_slots[i].pence);
      max_val = std::max(max_val, rates.export_slots[i].pence);
    }
  }
  max_val = std::max(max_val, min_val + 5.0f);  // Ensure non-zero range

  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, static_cast<lv_coord_t>(min_val),
                     static_cast<lv_coord_t>(max_val));

  // Populate 48 points into graph series
  for (int i = 0; i < 48; ++i) {
    if (rates.import_valid && rates.import_slots[i].valid) {
      s_series_import->y_points[i] = static_cast<lv_coord_t>(rates.import_slots[i].pence);
    } else {
      s_series_import->y_points[i] = LV_CHART_POINT_NONE;
    }

    if (rates.export_valid && rates.export_slots[i].valid) {
      s_series_export->y_points[i] = static_cast<lv_coord_t>(rates.export_slots[i].pence);
    } else {
      s_series_export->y_points[i] = LV_CHART_POINT_NONE;
    }
  }

  lv_chart_set_cursor_point(s_chart, s_cursor, NULL, slot_idx);
  lv_chart_refresh(s_chart);
}

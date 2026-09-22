#include "screen_rates.h"

#include <stdio.h>
#include <time.h>
#include <algorithm>

#include "board_config.h"
#include "format.h"
#include "theme.h"

namespace {
constexpr lv_coord_t ROW_WIDTH = 250;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_title = nullptr;
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


// Chart
lv_obj_t* s_chart = nullptr;
lv_chart_series_t* s_series_import = nullptr;
lv_chart_series_t* s_series_export = nullptr;
lv_chart_cursor_t* s_cursor = nullptr;


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

// Chart area drawing call back
static void chart_draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t * dsc = (lv_obj_draw_part_dsc_t *)lv_event_get_param(e);

    // 1. Ensure we are capturing the data points rendering layer
    if(dsc->part == LV_PART_ITEMS) {
        // Ensure p1 and p2 exist (validating it's a connecting line segment)
        if(!dsc->p1 || !dsc->p2) return;

        // 2. Identify the active series to declare the target gradient color
        lv_color_t base_color;
        if(dsc->sub_part_ptr == s_series_import) {
          base_color = lv_color_hex(PUCK_COLOUR_IMPORT);
        } 
        else if(dsc->sub_part_ptr == s_series_export) {
          base_color = lv_color_hex(PUCK_COLOUR_EXPORT);
        } 
        else {
            return; // Unknown series layout
        }

        // 3. Create a slope boundary mask (clips everything ABOVE the drawn line segment)
        lv_draw_mask_line_param_t line_mask_param;
        lv_draw_mask_line_points_init(&line_mask_param, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y, LV_DRAW_MASK_LINE_SIDE_BOTTOM);
        int16_t line_mask_id = lv_draw_mask_add(&line_mask_param, NULL);

        // 4. Create a vertical fade mask (blends transparency from the line down to chart base)
        lv_draw_mask_fade_param_t fade_mask_param;
        lv_area_t fill_area;
        fill_area.x1 = dsc->p1->x;
        fill_area.x2 = dsc->p2->x;
        fill_area.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
        fill_area.y2 = obj->coords.y2; // Extends straight down to the chart's bottom boundary

        // Initialise fade: start with semi-transparency at the top, down to absolute transparent at the base
        lv_draw_mask_fade_init(&fade_mask_param, &fill_area, LV_OPA_30, fill_area.y1, LV_OPA_TRANSP, fill_area.y2);
        int16_t fade_mask_id = lv_draw_mask_add(&fade_mask_param, NULL);

        // 5. Setup the draw rectangle properties
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = base_color;
        rect_dsc.bg_opa = LV_OPA_COVER; // The masking structures override this to create the transparency effect

        // 6. SAFE DRAWING CONTEXT IN LVGL 8.4: Extract directly from the parent descriptor structure
        // This completely avoids the 'Not interpreted with this event code' execution warning
        lv_draw_rect(dsc->draw_ctx, &rect_dsc, &fill_area);

        // 7. Clean up the masks from the engine's internal stack immediately after drawing
        lv_draw_mask_remove_id(line_mask_id);
        lv_draw_mask_remove_id(fade_mask_id);
    }
}


}  // namespace

lv_obj_t* screen_rates_create(lv_obj_t* parent) {
  s_root = make_group(parent);
  lv_obj_set_size(s_root, PUCK_LCD_WIDTH, PUCK_LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);


  // Chart Component
  s_chart = lv_chart_create(s_root);
  lv_obj_set_size(s_chart, 384, 100);
  lv_obj_align(s_chart, LV_ALIGN_CENTER, 0, 130);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, 48);
  lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_50, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(s_chart, LV_OPA_50, LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(s_chart, chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);


  // Background and grid lines
  lv_obj_set_style_bg_color(s_chart, lv_color_hex(PUCK_COLOUR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_chart, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_line_color(s_chart, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, 0, LV_PART_MAIN);
  lv_chart_set_div_line_count(s_chart, 8, 12);

  lv_obj_set_style_pad_all(s_chart, 0, 0);
  lv_obj_set_style_radius(s_chart, 0, 0);

  // Add Import & Export series
  s_series_import =
      lv_chart_add_series(s_chart, lv_color_hex(PUCK_COLOUR_IMPORT), LV_CHART_AXIS_PRIMARY_Y);
  s_series_export =
      lv_chart_add_series(s_chart, lv_color_hex(PUCK_COLOUR_EXPORT), LV_CHART_AXIS_PRIMARY_Y);

  // Vertical line cursor for active slot
  s_cursor = lv_chart_add_cursor(s_chart, lv_color_hex(PUCK_COLOUR_MUTED), LV_DIR_VER);
  lv_obj_set_style_line_opa(s_chart, LV_OPA_50, LV_PART_CURSOR | LV_STATE_DEFAULT);


  // Title
  s_title = make_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
  lv_label_set_text(s_title, "TARIFF RATES");
  lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -170);


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
  // lv_obj_align(s_slots_box, LV_ALIGN_CENTER, 0, 90);
  lv_obj_align(s_slots_box, LV_ALIGN_CENTER, 0, 50);

  for (size_t i = 0; i < SNAPSHOT_MAX_TARIFF_SLOTS; ++i) {
    lv_obj_t* row = make_group(s_slots_box);
    lv_obj_set_size(row, ROW_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s_slot_rows[i] = row;

    s_slot_when[i] = make_label(row, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED);
    lv_obj_set_width(s_slot_when[i], 70);
    lv_obj_set_style_text_align(s_slot_when[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_slot_when[i], "");

    s_slot_price_imp[i] = make_label(row, PUCK_FONT_BODY, PUCK_COLOUR_TEXT);
    lv_obj_set_width(s_slot_price_imp[i], 90);
    lv_obj_set_style_text_align(s_slot_price_imp[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(s_slot_price_imp[i], "");

    s_slot_price_exp[i] = make_label(row, PUCK_FONT_BODY, PUCK_COLOUR_EXPORT);
    lv_obj_set_width(s_slot_price_exp[i], 90);
    lv_obj_set_style_text_align(s_slot_price_exp[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(s_slot_price_exp[i], "");
  }

  s_unconfigured = make_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_MUTED);
  lv_label_set_text(s_unconfigured, "no tariff set");
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
  const bool configured = snapshot.valid && rates.import_valid && rates.export_valid;

  if (!configured) {
    lv_label_set_text(s_rate_imp_now, "");
    lv_label_set_text(s_rate_exp_now, "");
    lv_obj_add_flag(s_slots_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_unconfigured, snapshot.valid ? "no tariff set" : "offline");
    
    lv_obj_add_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
    // lv_obj_clear_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(s_slots_box, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_unconfigured, LV_OBJ_FLAG_HIDDEN);

  // Position vertical line cursor at active slot index based on timestamp
  uint8_t slot_idx = 0;
  time_t now = snapshot.ts != 0 ? static_cast<time_t>(snapshot.ts) : time(nullptr);
  struct tm tm_now = {};
  if (localtime_r(&now, &tm_now) != nullptr) {
    uint8_t calculated = (tm_now.tm_hour * 2) + (tm_now.tm_min >= 30 ? 1 : 0);
    slot_idx = calculated < 48 ? calculated : 47;
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

  // Textual values and slots
  char text[32];
  char scratch[16];

  const bool time_valid = (localtime_r(&now, &tm_now) != nullptr);
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




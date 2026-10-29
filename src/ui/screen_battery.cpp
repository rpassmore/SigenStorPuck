#include "screen_battery.h"

#include <stdio.h>

#include "board_config.h"
#include "chart_band.h"
#include "format.h"
#include "theme.h"
#include "ui_bezel_ring.h"
#include "ui_common.h"
#include "ui_metric_pill.h"
#include "ui_stat_grid.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_band = nullptr;
UiBezelRing s_arc;
UiScreenHeader s_header;
lv_obj_t* s_stored = nullptr;
UiMetricPill s_pill;
lv_obj_t* s_eta = nullptr;
UiStatGrid2x2 s_stats;
bool s_live = true;

// Formats one of the day's totals, or a dash when the day cannot be read.
void set_day_total(lv_obj_t* label, const Snapshot& snapshot, const MaybeFloat& value) {
  char text[24];
  if (!snapshot.valid || !snapshot.today.present) {
    lv_label_set_text(label, "--");
    return;
  }
  puck_format_kwh(value, 1, text, sizeof(text));
  lv_label_set_text(label, text);
}

}  // namespace

lv_obj_t* screen_battery_create(lv_obj_t* parent) {
  s_root = puck_create_screen_root(parent);

  s_arc.create(s_root, PUCK_COLOUR_BATTERY);

  // Before the labels, so they sit on top of it.
  // Fixed 0-100 rather than autoscaled: the height of this curve should mean
  // the same thing every time you look at it.
  s_band = chart_band_create_standard(s_root, HistorySeries::Soc, PUCK_COLOUR_BATTERY, 0.0f, 100.0f);

  s_header.create(s_root, "BATTERY", nullptr, PUCK_COLOUR_TEXT, -166);

  s_stored = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -70);
  lv_label_set_text(s_stored, "");

  s_pill.create(s_root, PUCK_COLOUR_BATTERY, -26);

  s_eta = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, 8);
  lv_label_set_text(s_eta, "");

  // Four figures over the curve: CHARGED, DISCHARGED, HEALTH, TEMP
  s_stats.create(s_root, "CHARGED", "DISCHARGED", "HEALTH", "TEMP");

  return s_root;
}

void screen_battery_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  char text[48];
  char scratch[24];

  if (s_band != nullptr) {
    chart_band_refresh(s_band);
  }

  const Snapshot::Battery* battery = nullptr;
  if (snapshot.valid) {
    battery = &snapshot.battery;
  }

  // State of charge, and the arc.
  if (battery != nullptr && battery->soc_pct.known) {
    snprintf(text, sizeof(text), "%.0f%%", battery->soc_pct.value);
    s_header.set_hero(text);
    s_arc.set_value(static_cast<int16_t>(battery->soc_pct.value));
    const bool low = battery->soc_pct.value < PUCK_SOC_LOW_PCT;
    lv_obj_set_style_arc_color(s_arc.arc(),
                               lv_color_hex(low ? PUCK_COLOUR_WARN : PUCK_COLOUR_BATTERY),
                               LV_PART_INDICATOR);
  } else {
    s_header.set_hero("--");
    s_arc.set_value(0);
  }

  // Energy stored / capacity.
  if (battery != nullptr && battery->soc_pct.known && battery->capacity_kwh.known) {
    const float stored = battery->capacity_kwh.value * battery->soc_pct.value / 100.0f;
    snprintf(text, sizeof(text), "%.1f of %.1f kWh", stored, battery->capacity_kwh.value);
    lv_label_set_text(s_stored, text);
  } else if (battery != nullptr && battery->capacity_kwh.known) {
    snprintf(text, sizeof(text), "%.1f kWh capacity", battery->capacity_kwh.value);
    lv_label_set_text(s_stored, text);
  } else {
    lv_label_set_text(s_stored, "");
  }

  // The pill and the ETA are live-only, so a past day simply does not have them.
  lv_obj_t* const live_only[] = {s_pill.container(), s_eta};
  puck_set_visible(live_only, sizeof(live_only) / sizeof(live_only[0]), s_live);

  // Charge or discharge rate, in the pill.
  const MaybeFloat rate = snapshot.valid ? snapshot.power.batt : MaybeFloat{};
  const bool moving = rate.known && rate.value != 0.0f;
  const bool charging = rate.known && rate.value > 0.0f;
  if (!rate.known) {
    s_pill.set_text("-- kW");
  } else if (!moving) {
    s_pill.set_text("idle");
  } else {
    puck_format_magnitude(rate, PUCK_KW_DECIMALS, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "%s kW %s", scratch, charging ? "charging" : "discharging");
    s_pill.set_text(text);
  }
  s_pill.set_active(moving);

  // ETA to full / empty
  if (battery != nullptr && battery->eta_min.known && moving) {
    puck_format_duration(battery->eta_min, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "%s in %s", charging ? "full" : "empty", scratch);
    lv_label_set_text(s_eta, text);
  } else {
    lv_label_set_text(s_eta, "");
  }

  set_day_total(s_stats.top_left.value, snapshot, snapshot.today.charge);
  set_day_total(s_stats.top_right.value, snapshot, snapshot.today.discharge);

  if (battery != nullptr && battery->soh_pct.known) {
    snprintf(text, sizeof(text), "%.0f%%", battery->soh_pct.value);
    s_stats.bottom_left.set(text);
  } else {
    s_stats.bottom_left.set("--");
  }

  if (battery != nullptr && battery->temp_c.known) {
    snprintf(text, sizeof(text), "%.1f°C", battery->temp_c.value);
    s_stats.bottom_right.set(text);
  } else {
    s_stats.bottom_right.set("--");
  }
}

void screen_battery_set_live(bool live) {
  s_live = live;
}

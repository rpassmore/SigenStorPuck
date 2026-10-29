#include "screen_load.h"

#include <stdio.h>

#include "board_config.h"
#include "chart_band.h"
#include "format.h"
#include "theme.h"
#include "ui_common.h"
#include "ui_metric_pill.h"
#include "ui_stat_grid.h"

namespace {

constexpr lv_opa_t BAND_GHOST = 80;
constexpr uint8_t BAND_SMOOTHING = 15;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_band = nullptr;
UiScreenHeader s_header;
UiMetricPill s_pill;
UiStatGrid2x2 s_stats;
bool s_live = true;
bool s_with_server = true;

// The four figures all come from the day's flow split, which only a server has.
void set_figure(lv_obj_t* label, bool known, float kwh) {
  char text[24];
  if (!known) {
    lv_label_set_text(label, "--");
    return;
  }
  snprintf(text, sizeof(text), "%.1f kWh", kwh);
  lv_label_set_text(label, text);
}

}  // namespace

lv_obj_t* screen_load_create(lv_obj_t* parent, bool with_server) {
  s_with_server = with_server;
  s_root = puck_create_screen_root(parent);

  // Autoscaled load curve; ghost 80 (since PUCK_COLOUR_HOME is nearly white), smoothing 15
  s_band = chart_band_create_standard(s_root, HistorySeries::Load, PUCK_COLOUR_HOME, 0.0f, 0.0f);
  if (s_band != nullptr) {
    chart_band_set_intensity(s_band, BAND_GHOST);
    chart_band_set_smoothing(s_band, BAND_SMOOTHING);
  }

  s_header.create(s_root, "LOAD", "used", PUCK_COLOUR_TEXT, -166);

  s_pill.create(s_root, PUCK_COLOUR_HOME, -26);

  if (!s_with_server) {
    return s_root;
  }

  // The split first, then where it came from: HOUSE, EV, FROM SOLAR, FROM GRID
  s_stats.create(s_root, "HOUSE", "EV", "FROM SOLAR", "FROM GRID");

  return s_root;
}

void screen_load_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  char text[48];
  char scratch[24];

  if (s_band != nullptr) {
    chart_band_refresh(s_band);
  }

  const bool have = snapshot.valid && snapshot.today.present;
  const Snapshot::Today::Flows& f = snapshot.today.flows;

  // Headline load total
  if (have && snapshot.today.load.known) {
    puck_format_kwh(snapshot.today.load, 1, text, sizeof(text));
    s_header.set_hero(text);
  } else {
    s_header.set_hero("--");
  }

  // The pill is live-only
  s_pill.set_visible(s_live);

  // What is being drawn right now — the house plus the car
  const MaybeFloat home = snapshot.valid ? snapshot.power.home : MaybeFloat{};
  const float ev_now = snapshot.valid && snapshot.power.ev.known ? snapshot.power.ev.value : 0.0f;
  if (!home.known) {
    s_pill.set_text("-- kW");
  } else if (home.value + ev_now <= 0.0f) {
    s_pill.set_text("idle");
  } else {
    const MaybeFloat total{true, home.value + ev_now};
    puck_format_magnitude(total, PUCK_KW_DECIMALS, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "%s kW now", scratch);
    s_pill.set_text(text);
  }

  if (!s_with_server) {
    return;
  }

  const bool split = have && f.solar_load.known;
  const float ev_day = (f.solar_ev.known ? f.solar_ev.value : 0.0f) +
                       (f.batt_ev.known ? f.batt_ev.value : 0.0f) +
                       (f.grid_ev.known ? f.grid_ev.value : 0.0f);
  const float total_day = have && snapshot.today.load.known ? snapshot.today.load.value : 0.0f;
  const float house_day = total_day - ev_day > 0.0f ? total_day - ev_day : 0.0f;

  set_figure(s_stats.top_left.value, split, house_day);
  set_figure(s_stats.top_right.value, split, ev_day);
  set_figure(s_stats.bottom_left.value, split, f.solar_load.known ? f.solar_load.value : 0.0f);
  set_figure(s_stats.bottom_right.value, f.grid_load.known, f.grid_load.known ? f.grid_load.value : 0.0f);
}

void screen_load_set_live(bool live) {
  s_live = live;
}

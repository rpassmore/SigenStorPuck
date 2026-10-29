#include "screen_solar.h"

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

// Smoothing for solar band is wider (13) to smooth out broken cloud picket fence.
constexpr uint8_t BAND_SMOOTHING = 13;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_band = nullptr;
UiBezelRing s_arc;
UiScreenHeader s_header;
UiMetricPill s_pill;
UiStatGrid2x2 s_stats;
bool s_live = true;

void set_forecast_figure(lv_obj_t* label, const Snapshot& snapshot, const MaybeFloat& value,
                         int decimals, const char* unit) {
  if (!snapshot.valid || !snapshot.solar.configured || !value.known) {
    lv_label_set_text(label, "--");
    return;
  }
  char scratch[16];
  char text[24];
  puck_format_magnitude(value, decimals, scratch, sizeof(scratch));
  snprintf(text, sizeof(text), "%s%s", scratch, unit);
  lv_label_set_text(label, text);
}

}  // namespace

lv_obj_t* screen_solar_create(lv_obj_t* parent) {
  s_root = puck_create_screen_root(parent);

  s_arc.create(s_root, PUCK_COLOUR_SOLAR, /*hidden=*/true);

  // Autoscaled PV curve
  s_band = chart_band_create_standard(s_root, HistorySeries::Pv, PUCK_COLOUR_SOLAR, 0.0f, 0.0f);
  if (s_band != nullptr) {
    chart_band_set_smoothing(s_band, BAND_SMOOTHING);
  }

  s_header.create(s_root, "SOLAR", "generated", PUCK_COLOUR_TEXT, -166);

  s_pill.create(s_root, PUCK_COLOUR_SOLAR, -26);

  s_stats.create(s_root, "FORECAST", "REMAINING", "VS FORECAST", "PEAK");

  return s_root;
}

void screen_solar_update(const Snapshot& snapshot) {
  if (s_root == nullptr) {
    return;
  }

  char text[48];
  char scratch[24];

  if (s_band != nullptr) {
    chart_band_refresh(s_band);
  }

  // Today's generation.
  const bool have_today = snapshot.valid && snapshot.today.present;
  const MaybeFloat generated = have_today ? snapshot.today.solar : MaybeFloat{};
  if (generated.known) {
    puck_format_kwh(generated, 1, text, sizeof(text));
    s_header.set_hero(text);
  } else {
    s_header.set_hero("--");
  }

  // The ring: how much of today's forecast has been generated.
  const MaybeFloat forecast =
      snapshot.valid && snapshot.solar.configured ? snapshot.solar.forecast_kwh : MaybeFloat{};
  if (generated.known && forecast.known && forecast.value > 0.0f) {
    float pct = generated.value / forecast.value * 100.0f;
    if (pct < 0.0f) {
      pct = 0.0f;
    }
    if (pct > 100.0f) {
      pct = 100.0f;
    }
    s_arc.set_value(static_cast<int16_t>(pct));
    s_arc.set_visible(true);
  } else {
    s_arc.set_visible(false);
  }

  // The pill is live-only.
  s_pill.set_visible(s_live);

  // Live PV, in the pill.
  const MaybeFloat pv = snapshot.valid ? snapshot.power.pv : MaybeFloat{};
  const bool generating = pv.known && pv.value > 0.0f;
  if (!pv.known) {
    s_pill.set_text("-- kW");
  } else if (!generating) {
    s_pill.set_text("idle");
  } else {
    puck_format_magnitude(pv, PUCK_KW_DECIMALS, scratch, sizeof(scratch));
    snprintf(text, sizeof(text), "%s kW generating", scratch);
    s_pill.set_text(text);
  }
  s_pill.set_active(generating);

  set_forecast_figure(s_stats.top_left.value, snapshot, snapshot.solar.forecast_kwh, 1, " kWh");
  set_forecast_figure(s_stats.top_right.value, snapshot, snapshot.solar.remaining_kwh, 1, " kWh");
  set_forecast_figure(s_stats.bottom_left.value, snapshot, snapshot.solar.vs_forecast_pct, 0, "%");
  set_forecast_figure(s_stats.bottom_right.value, snapshot, snapshot.solar.peak_kw, 1, " kW");
}

void screen_solar_set_live(bool live) {
  s_live = live;
}

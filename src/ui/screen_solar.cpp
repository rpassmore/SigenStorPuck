#include "screen_solar.h"

#include <stdio.h>

#include "board_config.h"
#include "chart_band.h"
#include "forecast_store.h"
#include "format.h"
#include "history.h"
#include "psram_alloc.h"
#include "solar_metric_layout.h"
#include "theme.h"
#include "ui_bezel_ring.h"
#include "ui_common.h"
#include "ui_metric_pill.h"
#include "ui_stat_grid.h"

namespace {

// Smoothing for solar band is wider (13) to smooth out broken cloud picket fence.
constexpr uint8_t BAND_SMOOTHING = 13;
constexpr lv_opa_t FORECAST_GHOST = 46;

// 288 columns for the forecast band, allocated in PSRAM by screen_solar_create().
HistoryColumn* s_forecast_cols = nullptr;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_forecast_band = nullptr;
lv_obj_t* s_band = nullptr;
UiBezelRing s_arc;
UiScreenHeader s_header;
UiMetricPill s_pill;
UiStatGrid2x2 s_stats;
bool s_live = true;
SolarOptionalMetricSlots s_slots;

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

// void position_optional_figure(lv_obj_t* caption, lv_obj_t* value, int8_t slot) {
//   if (slot == SolarOptionalMetricSlots::Hidden) {
//     lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
//     return;
//   }

//   // Total forecast owns slot zero. A figure the source can never supply leaves no
//   // hole: the ones it can supply close up behind the total.
//   const bool right = (slot & 1) != 0;
//   const bool second_row = slot >= 2;
//   const lv_coord_t x = right ? COLUMN_X : -COLUMN_X;
//   const lv_coord_t caption_y = second_row ? ROW_TWO_LABEL_Y : ROW_ONE_LABEL_Y;
//   const lv_coord_t value_y = second_row ? ROW_TWO_VALUE_Y : ROW_ONE_VALUE_Y;
//   lv_obj_align(caption, LV_ALIGN_CENTER, x, caption_y);
//   lv_obj_align(value, LV_ALIGN_CENTER, x, value_y);
//   lv_obj_clear_flag(caption, LV_OBJ_FLAG_HIDDEN);
//   lv_obj_clear_flag(value, LV_OBJ_FLAG_HIDDEN);
// }

}  // namespace

lv_obj_t* screen_solar_create(lv_obj_t* parent, uint8_t figures) {
  s_root = puck_create_screen_root(parent);

  s_arc.create(s_root, PUCK_COLOUR_SOLAR, /*hidden=*/true);

  // Created before the actual band so it draws behind it. Fed from forecast_store
  // (the whole day, future included), not the history ring — see forecast_store.h.
  // Same geometry and bezel clip as the actual, just fainter.
  s_forecast_cols = static_cast<HistoryColumn*>(psram_alloc(288 * sizeof(HistoryColumn)));
  s_forecast_band = chart_band_create_standard(s_root, HistorySeries::Pv, PUCK_COLOUR_SOLAR, 0.0f, 0.0f);
  if (s_forecast_band != nullptr) {
    chart_band_set_intensity(s_forecast_band, FORECAST_GHOST);
    chart_band_set_smoothing(s_forecast_band, BAND_SMOOTHING);
  }

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

  // The forecast curve behind the actual generation, sharing its vertical scale
  // so "generated so far" sits under the forecast line rather than being rescaled
  // to its own smaller peak. Only on today's own day (forecast_store_columns
  // refuses a stepped-back day) and only when the source supplies a forecast;
  // otherwise the actual band goes back to auto-scaling on its own.
  if (s_forecast_band != nullptr) {
    uint32_t from = 0;
    uint32_t to = 0;
    float peak = 0.0f;
    const size_t n = chart_band_column_count(s_forecast_band);
    if (n > 0 && s_forecast_cols != nullptr && snapshot.valid && snapshot.solar.configured &&
        history_window(history_view(), &from, &to) &&
        forecast_store_columns(from, to, s_forecast_cols, n, &peak) && peak > 0.0f) {
      chart_band_set_columns(s_forecast_band, s_forecast_cols, n, 0.0f, peak);
      chart_band_set_range(s_band, 0.0f, peak);
    } else {
      chart_band_clear(s_forecast_band);
      chart_band_set_range(s_band, 0.0f, 0.0f);  // no forecast: actual auto-scales
    }
  }

  if (s_band != nullptr) {
    chart_band_refresh(s_band);
  }

  // Today's generation. today.solar rather than a field of its own in the solar
  // block, so this figure is the same one on every data source — Modbus fills it
  // from pv_daily registers and HA from the configured daily entity.
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
  if (s_slots.remaining != SolarOptionalMetricSlots::Hidden) {
      set_forecast_figure(s_stats.top_right.value, snapshot, snapshot.solar.remaining_kwh, 1, " kWh");
  }
  if (s_slots.vs_forecast != SolarOptionalMetricSlots::Hidden) {
    set_forecast_figure(s_stats.bottom_left.value, snapshot, snapshot.solar.vs_forecast_pct, 0, "%");
  }
  if (s_slots.peak != SolarOptionalMetricSlots::Hidden) {
    set_forecast_figure(s_stats.bottom_right.value, snapshot, snapshot.solar.peak_kw, 1, " kW");
  }
}

void screen_solar_set_live(bool live) {
  s_live = live;
}

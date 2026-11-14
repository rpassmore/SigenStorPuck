#include "ui_power_arc.h"

#include <math.h>
#include <cstdlib>

#include "board_config.h"
#include "theme.h"
#include "ui_common.h"

void ui_power_arc_draw_zero_marker(lv_draw_ctx_t* draw_ctx, lv_point_t center,
                                   uint16_t radius, int32_t arc_width, int32_t angle_deg) {
  if (arc_width <= 0) return;

  float angle_rad = angle_deg * (float)M_PI / 180.0f;
  float cos_a = cosf(angle_rad);
  float sin_a = sinf(angle_rad);

  // Position the dot on the ring's midline (halfway through its thickness)
  int32_t mid_r = radius - (arc_width / 2);
  int32_t cx = center.x + (int32_t)(mid_r * cos_a);
  int32_t cy = center.y + (int32_t)(mid_r * sin_a);

  int32_t dot_d = arc_width;
  if (dot_d < 4) dot_d = 4;  // floor so it doesn't vanish on thin rings

  lv_area_t area;
  area.x1 = cx - dot_d / 2;
  area.y1 = cy - dot_d / 2;
  area.x2 = cx + dot_d / 2;
  area.y2 = cy + dot_d / 2;

  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = lv_color_hex(POWER_COLOUR_ZERO_MARKER);
  rect_dsc.bg_opa = LV_OPA_COVER;
  rect_dsc.border_width = 0;

  lv_draw_rect(draw_ctx, &rect_dsc, &area);
}

void ui_power_arc_draw_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code != LV_EVENT_DRAW_PART_BEGIN) {
    return;
  }

  lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);

  if (dsc->class_p != &lv_arc_class || dsc->type != LV_ARC_DRAW_PART_FOREGROUND) {
    return;
  }

  lv_obj_t* arc_obj = lv_event_get_target(e);

  lv_point_t center;
  center.x = dsc->p1->x;
  center.y = dsc->p1->y;
  uint16_t radius = dsc->radius;
  int32_t arc_width = dsc->arc_dsc->width;  // snapshot before we zero it out below

  const int32_t origin_angle = 180;  // 9:00 in LVGL's screen-angle convention

  int32_t current_val = lv_arc_get_value(arc_obj);
  int32_t start_angle = lv_arc_get_angle_start(arc_obj);
  int32_t end_angle = lv_arc_get_angle_end(arc_obj);
  int32_t total_angle_sweep = end_angle - start_angle;
  if (total_angle_sweep < 0) {
    total_angle_sweep += 360;
  }

  if (current_val != 0 && total_angle_sweep > 0) {
    lv_draw_arc_dsc_t slice_dsc;
    lv_memcpy(&slice_dsc, dsc->arc_dsc, sizeof(lv_draw_arc_dsc_t));
    slice_dsc.opa = LV_OPA_COVER;
    slice_dsc.width = arc_width;

    const lv_color_t c_white = lv_color_hex(POWER_COLOUR_WHITE);
    const lv_color_t c_imp_mid = lv_color_hex(POWER_COLOUR_IMP_MID);
    const lv_color_t c_imp_high = lv_color_hex(POWER_COLOUR_IMP_HIGH);
    const lv_color_t c_exp_mid = lv_color_hex(POWER_COLOUR_EXP_MID);
    const lv_color_t c_exp_high = lv_color_hex(POWER_COLOUR_EXP_HIGH);

    for (int32_t deg = 0; deg < total_angle_sweep; deg++) {
      int32_t slice_val = (deg * std::abs(current_val)) / total_angle_sweep;
      lv_color_t slice_color;

      if (current_val > 0) {
        if (slice_val <= s_gradient_config.imp_mid_threshold) {
          // import mid
          int32_t range = s_gradient_config.imp_mid_threshold;
          int32_t ratio = (range > 0) ? (slice_val * 255) / range : 255;
          slice_color = lv_color_mix(c_imp_mid, c_white, ratio);
        } else if (slice_val <= s_gradient_config.imp_high_threshold) {
          // import high
          int32_t range = s_gradient_config.imp_high_threshold - s_gradient_config.imp_mid_threshold;
          int32_t ratio = (range > 0) ? ((slice_val - s_gradient_config.imp_mid_threshold) * 255) / range : 255;
          slice_color = lv_color_mix(c_imp_high, c_imp_mid, ratio);
        } else {
          slice_color = c_imp_high;
        }
      } else {
        if (slice_val <= s_gradient_config.exp_mid_threshold) {
          // export mid
          int32_t range = s_gradient_config.exp_mid_threshold;
          int32_t ratio = (range > 0) ? (slice_val * 255) / range : 255;
          slice_color = lv_color_mix(c_exp_mid, c_white, ratio);
        } else if (slice_val <= s_gradient_config.exp_high_threshold) {
          // export high
          int32_t range = s_gradient_config.exp_high_threshold - s_gradient_config.exp_mid_threshold;
          int32_t ratio = (range > 0) ? ((slice_val - s_gradient_config.exp_mid_threshold) * 255) / range : 255;
          slice_color = lv_color_mix(c_exp_high, c_exp_mid, ratio);
        } else {
          slice_color = c_exp_high;
        }
      }

      slice_dsc.color = slice_color;

      int32_t target_deg = (current_val > 0)
                               ? origin_angle + deg
                               : origin_angle - deg - 1;
      lv_draw_arc(dsc->draw_ctx, &slice_dsc, &center, radius, target_deg, target_deg + 1);
    }
  }

  // Always draw a zero-position tick, regardless of current_val
  ui_power_arc_draw_zero_marker(dsc->draw_ctx, center, radius, arc_width, origin_angle);

  // Suppress default single-color theme lines
  dsc->arc_dsc->width = 0;
  dsc->arc_dsc->opa = LV_OPA_TRANSP;
}

void UiPowerArc::create(lv_obj_t* parent) {
  ring_ = lv_arc_create(parent);
  lv_obj_set_size(ring_, PUCK_RING_DIAMETER, PUCK_RING_DIAMETER);
  lv_obj_center(ring_);
  lv_arc_set_rotation(ring_, 270);  // zero at the left
  lv_arc_set_range(ring_, -8000, 8000);
  lv_arc_set_mode(ring_, LV_ARC_MODE_SYMMETRICAL);
  lv_arc_set_value(ring_, 0);
  lv_obj_remove_style(ring_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(ring_, PUCK_RING_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring_, PUCK_RING_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring_, lv_color_hex(PUCK_COLOUR_TRACK), LV_PART_MAIN);
  lv_obj_add_event_cb(ring_, ui_power_arc_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
}

void UiPowerArc::set_value_watts(int32_t watts) {
  if (ring_ == nullptr) return;
  lv_arc_set_value(ring_, watts);
  lv_obj_invalidate(ring_);
}

void UiPowerArc::update(const MaybeFloat& grid_power_kw) {
  if (ring_ == nullptr) return;
  if (grid_power_kw.known) {
    float pwrWatts = grid_power_kw.value * 1000.0f;
    if (pwrWatts < -10000.0f) pwrWatts = -10000.0f;
    if (pwrWatts > 10000.0f) pwrWatts = 10000.0f;
    set_value_watts(static_cast<int32_t>(lroundf(pwrWatts)));
  } else {
    lv_arc_set_value(ring_, 0);
  }
}

void UiPowerArc::set_visible(bool visible) {
  puck_set_visible(ring_, visible);
}

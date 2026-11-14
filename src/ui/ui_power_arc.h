// Symmetrical power arc helper for rendering import/export power on the bezel ring.

#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "power_colour.h"
#include "snapshot.h"

// LVGL draw event callback for custom symmetrical gradient sweep arc rendering.
// Handles both import (positive) and export (negative) power sweeps with gradient coloring
// and draws a zero position tick marker.
void ui_power_arc_draw_cb(lv_event_t* e);

// Draws a zero position tick marker on the ring midline.
void ui_power_arc_draw_zero_marker(lv_draw_ctx_t* draw_ctx, lv_point_t center,
                                   uint16_t radius, int32_t arc_width, int32_t angle_deg);

// Encapsulates the bezel ring power arc gauge.
class UiPowerArc {
 public:
  void create(lv_obj_t* parent);
  void update(const MaybeFloat& grid_power_kw);
  void set_value_watts(int32_t watts);
  void set_visible(bool visible);

  lv_obj_t* arc() const { return ring_; }

 private:
  lv_obj_t* ring_ = nullptr;
};

// Bezel ring arc component for screens displaying full-perimeter state.

#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "snapshot.h"

class UiBezelRing {
 public:
  void create(lv_obj_t* parent, uint32_t indicator_colour, bool hidden = false);
  void set_value(int32_t percent);
  void set_value(const MaybeInt& val);
  void set_visible(bool visible);

  lv_obj_t* arc() const { return arc_; }

 private:
  lv_obj_t* arc_ = nullptr;
};

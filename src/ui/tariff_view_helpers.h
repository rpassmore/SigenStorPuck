// Tariff view helpers shared between screen_cost and screen_rates.

#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "snapshot.h"

// Returns colour based on price comparison to the current rate:
// battery green (cheaper than now), warning amber (dearer than now), or muted (same rate).
uint32_t puck_colour_for_price(float pence, const MaybeFloat& rate_now);

// Creates standard slots container (ROW_WIDTH = 250, column flex, row gap 6) at given y offset.
lv_obj_t* puck_create_tariff_slots_box(lv_obj_t* parent, lv_coord_t y_offset = 90);

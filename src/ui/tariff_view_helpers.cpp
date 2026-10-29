#include "tariff_view_helpers.h"

#include "theme.h"
#include "ui_common.h"

uint32_t puck_colour_for_price(float pence, const MaybeFloat& rate_now) {
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

lv_obj_t* puck_create_tariff_slots_box(lv_obj_t* parent, lv_coord_t y_offset) {
  constexpr lv_coord_t ROW_WIDTH = 250;
  lv_obj_t* box = puck_create_group(parent);
  lv_obj_set_size(box, ROW_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 6, LV_PART_MAIN);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, y_offset);
  return box;
}

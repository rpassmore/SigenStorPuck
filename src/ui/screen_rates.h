// Screen: Daily Tariff Rates Graph & Current Rates display.

#pragma once

#include <lvgl.h>
#include "snapshot.h"

lv_obj_t* screen_rates_create(lv_obj_t* parent);
void screen_rates_update(const Snapshot& snapshot);

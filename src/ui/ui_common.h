// Foundational LVGL helpers and primitives shared across screens.

#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

// Creates a standard fullscreen black non-scrollable root container.
lv_obj_t* puck_create_screen_root(lv_obj_t* parent);

// Creates an unpadded, borderless, transparent, non-scrollable layout container.
lv_obj_t* puck_create_group(lv_obj_t* parent);

// Unified label factory with font, color, and optional alignment.
lv_obj_t* puck_create_label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour,
                            lv_coord_t x = 0, lv_coord_t y = 0,
                            lv_align_t align = LV_ALIGN_CENTER);

// Standard small muted caption with tracked letter spacing.
lv_obj_t* puck_create_caption(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y,
                              uint8_t letter_space = 2);

// Toggles LV_OBJ_FLAG_HIDDEN.
void puck_set_visible(lv_obj_t* obj, bool visible);

// Batch visibility toggle.
void puck_set_visible(lv_obj_t* const objs[], size_t count, bool visible);

// Sets text or "--" when valid is false.
void puck_set_label_or_dash(lv_obj_t* label, const char* text, bool valid);

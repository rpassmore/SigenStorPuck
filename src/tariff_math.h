#pragma once

#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include "snapshot.h"

// Format a Unix timestamp to UTC ISO 8601 string (e.g. "2024-01-01T12:00:00Z").
// Returns true on success, false if out_size is too small or tm conversion fails.
bool tariff_format_iso8601_utc(time_t t, char* out_buf, size_t out_size);

// Parse an Octopus ISO 8601 UTC timestamp string (e.g. "2024-01-01T12:00:00Z"
// or with timezone offsets like "+01:00") to Unix time.
bool tariff_parse_iso8601(const char* value, time_t* out);

// Given a Unix timestamp, get the start and end of the UTC calendar day.
bool tariff_get_utc_day_bounds(time_t now, time_t* day_start, time_t* day_end);

// Given an API rate interval [valid_from, valid_to), project the rate onto the
// fixed 48 local half-hour slots for the day [day_start, day_end).
void tariff_apply_interval(RateSlot slots[TARIFF_SLOTS_PER_DAY],
                           time_t day_start,
                           time_t day_end,
                           time_t valid_from,
                           time_t valid_to,
                           float pence);

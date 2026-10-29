#include "tariff_math.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr int64_t HALF_HOUR_SECONDS = 30 * 60;

// Convert a Gregorian UTC date/time to Unix time without depending on the
// device timezone. Octopus timestamps are returned as UTC (the trailing Z).
int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

bool tariff_format_iso8601_utc(time_t t, char* out_buf, size_t out_size) {
  if (!out_buf || out_size < 21) {
    return false;
  }
  struct tm tm_buf = {};
  if (gmtime_r(&t, &tm_buf) == nullptr) {
    return false;
  }
  const int written = snprintf(out_buf, out_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
  return written > 0 && static_cast<size_t>(written) < out_size;
}

bool tariff_parse_iso8601(const char* value, time_t* out) {
  if (!value || !out) {
    return false;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  const int fields = sscanf(value, "%d-%d-%dT%d:%d:%d", &year, &month, &day,
                            &hour, &minute, &second);
  if (fields < 5) {
    return false;
  }

  if (fields == 5) {
    second = 0;
  }

  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 60) {
    return false;
  }

  int64_t tz_offset_sec = 0;
  const char* tz_ptr = strchr(value, 'Z');
  if (!tz_ptr) {
    const char* plus = strchr(value + 10, '+');
    const char* minus = strchr(value + 10, '-');
    const char* off_ptr = plus ? plus : minus;
    if (off_ptr) {
      int tz_h = 0;
      int tz_m = 0;
      if (sscanf(off_ptr + 1, "%d:%d", &tz_h, &tz_m) >= 1) {
        tz_offset_sec = (tz_h * 3600LL + tz_m * 60LL) * (*off_ptr == '-' ? -1 : 1);
      }
    } else {
      return false;
    }
  }

  const int64_t days = days_from_civil(year, static_cast<unsigned>(month),
                                      static_cast<unsigned>(day));
  const int64_t seconds =
      days * 86400LL + hour * 3600LL + minute * 60LL + second - tz_offset_sec;

  *out = static_cast<time_t>(seconds);
  return true;
}

bool tariff_get_utc_day_bounds(time_t now, time_t* day_start, time_t* day_end) {
  if (!day_start || !day_end || now <= 0) {
    return false;
  }

  struct tm tm_utc = {};
  if (gmtime_r(&now, &tm_utc) == nullptr) {
    return false;
  }

  const int32_t sec_since_midnight =
      tm_utc.tm_hour * 3600 + tm_utc.tm_min * 60 + tm_utc.tm_sec;

  const time_t start = now - sec_since_midnight;
  const time_t end = start + 86400;

  *day_start = start;
  *day_end = end;
  return true;
}

void tariff_apply_interval(RateSlot slots[TARIFF_SLOTS_PER_DAY],
                           time_t day_start,
                           time_t day_end,
                           time_t valid_from,
                           time_t valid_to,
                           float pence) {
  if (valid_to <= valid_from || valid_to <= day_start || valid_from >= day_end) {
    return;
  }

  const time_t clipped_from = valid_from > day_start ? valid_from : day_start;
  const time_t clipped_to = valid_to < day_end ? valid_to : day_end;

  if (clipped_to <= clipped_from) {
    return;
  }

  for (int slot = 0; slot < TARIFF_SLOTS_PER_DAY; ++slot) {
    const time_t slot_start = day_start + static_cast<time_t>(slot * HALF_HOUR_SECONDS);
    const time_t slot_end = slot_start + static_cast<time_t>(HALF_HOUR_SECONDS);

    if (valid_from < slot_end && valid_to > slot_start) {
      slots[slot].pence = pence;
      slots[slot].valid = true;
    }
  }
}

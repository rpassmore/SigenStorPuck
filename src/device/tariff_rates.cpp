#include "tariff_rates.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "http_psram.h"
#include "net.h"
#include "poller.h"
#include "settings.h"
#include "tariff_math.h"

namespace {

constexpr uint32_t RETRY_MS = 60 * 1000;  // 1 min retry on error
constexpr uint16_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 10000;
constexpr int64_t HALF_HOUR_SECONDS = 30 * 60;
constexpr uint16_t MAX_API_PAGES = 3;
constexpr size_t PSRAM_HTTP_BUFFER_SIZE = 64 * 1024;

struct RateTargetState {
  char code[64] = {};
  uint32_t last_attempt_ms = 0;
  FetchResult last_result = FetchResult::NotConfigured;
  int last_fetch_day_of_year = -1;
  int last_fetch_min_of_day = -1;
};

DayTariffRates s_rates;
RateTargetState s_imp_state;
RateTargetState s_exp_state;
FetchResult s_last_result = FetchResult::NotConfigured;

bool build_tariff_url(const char* product_code, time_t day_start, time_t day_end,
                      char* out_buf, size_t out_size) {
  if (!product_code || product_code[0] == '\0' || !out_buf || out_size < 128) {
    return false;
  }

  // A full tariff code was provided like E-1R-AGILE-24-10-01-L or E-2R-AGILE-24-10-01-L.
  // Octopus tariff rate endpoint requires:
  // /v1/products/<product_code>/electricity-tariffs/<tariff_code>/standard-unit-rates/
  char extracted_product[64] = {};
  const bool is_full_tariff = (strncmp(product_code, "E-1R-", 5) == 0 ||
                               strncmp(product_code, "E-2R-", 5) == 0);

  if (is_full_tariff) {
    const char* last_dash = strrchr(product_code, '-');
    if (last_dash && (last_dash - product_code > 5)) {
      size_t prod_len = static_cast<size_t>(last_dash - (product_code + 5));
      if (prod_len >= sizeof(extracted_product)) {
        prod_len = sizeof(extracted_product) - 1;
      }
      memcpy(extracted_product, product_code + 5, prod_len);
      extracted_product[prod_len] = '\0';
    } else {
      snprintf(extracted_product, sizeof(extracted_product), "%s", product_code);
    }
    snprintf(out_buf, out_size,
             "https://api.octopus.energy/v1/products/%s/electricity-tariffs/%s/standard-unit-rates/",
             extracted_product, product_code);
  } else {
    // Standard product code like AGILE-24-10-01 -> assume Region L (South West)
    snprintf(out_buf, out_size,
             "https://api.octopus.energy/v1/products/%s/electricity-tariffs/E-1R-%s-L/standard-unit-rates/",
             product_code, product_code);
  }

  if (day_start > 0 && day_end > day_start) {
    char from_buf[32];
    char to_buf[32];
    tariff_format_iso8601_utc(day_start, from_buf, sizeof(from_buf));
    tariff_format_iso8601_utc(day_end, to_buf, sizeof(to_buf));

    const size_t cur_len = strlen(out_buf);
    if (cur_len < out_size) {
      snprintf(out_buf + cur_len, out_size - cur_len,
               "?period_from=%s&period_to=%s&page_size=100", from_buf, to_buf);
    }
  }

  return true;
}

void clear_slots(RateSlot slots[TARIFF_SLOTS_PER_DAY]) {
  for (int i = 0; i < TARIFF_SLOTS_PER_DAY; ++i) {
    slots[i].valid = false;
    slots[i].pence = 0.0f;
  }
}

FetchResult fetch_rates_for_code(const String& product_code, RateSlot slots[TARIFF_SLOTS_PER_DAY]) {
  if (product_code.isEmpty()) {
    return FetchResult::NotConfigured;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }

  if (!net_time_synced()) {
    return FetchResult::ClockUnset;
  }

  const time_t now = time(nullptr);

  time_t day_start = 0;
  time_t day_end = 0;
  if (!tariff_get_utc_day_bounds(now, &day_start, &day_end)) {
    return FetchResult::BadPayload;
  }

  RateSlot temp_slots[TARIFF_SLOTS_PER_DAY];
  clear_slots(temp_slots);

  char url[512] = {};
  if (!build_tariff_url(product_code.c_str(), day_start, day_end, url, sizeof(url))) {
    return FetchResult::BadPayload;
  }

  PsramBufferStream payload_buffer(PSRAM_HTTP_BUFFER_SIZE);
  if (!payload_buffer.isValid()) {
    return FetchResult::BadPayload;
  }

  int loaded_count = 0;
  bool saw_results = false;

  for (uint16_t page = 0; page < MAX_API_PAGES && url[0] != '\0'; ++page) {
    Serial.printf("url %s\n", url);
    const FetchResult http_res = http_get_psram(url, payload_buffer,
                                                CONNECT_TIMEOUT_MS, TOTAL_TIMEOUT_MS);
    if (http_res != FetchResult::Ok) {
      return http_res;
    }

    PsramAllocator json_allocator;
    JsonDocument doc(&json_allocator);

    // Filter to retain only relevant properties in PSRAM, discarding all unused JSON metadata
    JsonDocument filter;
    filter["next"] = true;
    JsonObject filter_entry = filter["results"].add<JsonObject>();
    filter_entry["valid_from"] = true;
    filter_entry["valid_to"] = true;
    filter_entry["value_inc_vat"] = true;

    const DeserializationError error =
        deserializeJson(doc, payload_buffer.c_str(), DeserializationOption::Filter(filter));
    if (error != DeserializationError::Ok) {
      return FetchResult::BadPayload;
    }

    JsonArrayConst results = doc["results"];
    if (results.isNull()) {
      return FetchResult::BadPayload;
    }

    saw_results = true;

    for (JsonObjectConst entry : results) {
      const char* valid_from_str = entry["valid_from"];
      const char* valid_to_str = entry["valid_to"];

      if (!valid_from_str) {
        continue;
      }

      time_t valid_from = 0;
      time_t valid_to = 0;

      if (!tariff_parse_iso8601(valid_from_str, &valid_from)) {
        continue;
      }

      if (valid_to_str != nullptr) {
        if (!tariff_parse_iso8601(valid_to_str, &valid_to)) {
          continue;
        }
      } else {
        // Open-ended interval (typical for fixed and ongoing variable tariffs)
        valid_to = day_end;
      }

      // The API interval is half-open: [valid_from, valid_to).
      if (valid_to <= day_start || valid_from >= day_end) {
        continue;
      }

      const float pence = entry["value_inc_vat"] | 0.0f;

      tariff_apply_interval(temp_slots, day_start, day_end,
                            valid_from, valid_to, pence);
      ++loaded_count;
    }

    // Octopus supplies an absolute URL in "next". Use it directly so this
    // works regardless of page size or query parameters chosen by the API.
    const char* next = doc["next"];
    if (next && next[0] != '\0') {
      strncpy(url, next, sizeof(url) - 1);
      url[sizeof(url) - 1] = '\0';
    } else {
      url[0] = '\0';
    }
  }

  if (!saw_results) {
    return FetchResult::BadPayload;
  }

  // Verify that at least one of the 48 slots was populated. A valid API
  // response for a product can legitimately contain records for other dates.
  for (int i = 0; i < TARIFF_SLOTS_PER_DAY; ++i) {
    if (temp_slots[i].valid) {
      memcpy(slots, temp_slots, sizeof(temp_slots));
      return FetchResult::Ok;
    }
  }

  return FetchResult::BadPayload;
}

// Drives periodic polling and change detection for a single tariff rate target (import or export).
void service_rate_target(const String& code,
                         RateTargetState& state,
                         RateSlot slots[TARIFF_SLOTS_PER_DAY],
                         bool& valid,
                         uint32_t now_ms,
                         time_t now,
                         const struct tm& tm_now,
                         uint16_t target_fetch_min,
                         const char* target_name) {
  if (code.isEmpty()) {
    if (valid || state.code[0] != '\0') {
      clear_slots(slots);
      valid = false;
      state = {};
    }
    return;
  }

  const uint16_t current_min = static_cast<uint16_t>(tm_now.tm_hour * 60 + tm_now.tm_min);
  const bool code_changed = (strcmp(code.c_str(), state.code) != 0);
  if (code_changed) {
    strncpy(state.code, code.c_str(), sizeof(state.code) - 1);
    state.code[sizeof(state.code) - 1] = '\0';
    state.last_attempt_ms = 0;
    state.last_result = FetchResult::NotConfigured;
    state.last_fetch_day_of_year = -1;
    state.last_fetch_min_of_day = -1;
    valid = false;
    clear_slots(slots);
  }

  const bool day_changed =
      (state.last_fetch_day_of_year != -1 &&
       tm_now.tm_yday != state.last_fetch_day_of_year);
  if (day_changed) {
    state.last_fetch_day_of_year = -1;
    state.last_fetch_min_of_day = -1;
    valid = false;
    clear_slots(slots);
  }

  const bool fetch_due =
      code_changed || day_changed || !valid ||
      (current_min >= target_fetch_min &&
       state.last_fetch_min_of_day < target_fetch_min);

  const bool retrying_too_soon =
      (state.last_result != FetchResult::Ok &&
       state.last_attempt_ms != 0 &&
       (now_ms - state.last_attempt_ms < RETRY_MS));

  if (fetch_due && !retrying_too_soon) {
    state.last_attempt_ms = now_ms;
    state.last_result = fetch_rates_for_code(code, slots);
    valid = (state.last_result == FetchResult::Ok);
    if (valid) {
      state.last_fetch_day_of_year = tm_now.tm_yday;
      state.last_fetch_min_of_day = current_min;
      s_rates.fetched_time = static_cast<uint32_t>(now);
      Serial.printf("[tariff_rates] successfully loaded day rates for %s (%s)\n",
                    target_name, code.c_str());
    } else {
      Serial.printf("[tariff_rates] %s rate fetch failed (%s): %s\n",
                    target_name, code.c_str(), fetch_result_name(state.last_result));
    }
  }
}

}  // namespace

FetchResult tariff_rates_service() {
  const Settings& settings = settings_get();
  const String& imp_code = settings.tariff_import_code;
  const String& exp_code = settings.tariff_export_code;

  if (imp_code.isEmpty() && exp_code.isEmpty()) {
    s_rates.import_valid = false;
    s_rates.export_valid = false;
    s_imp_state = {};
    s_exp_state = {};
    s_last_result = FetchResult::NotConfigured;
    return s_last_result;
  }

  // If WiFi or clock is not ready yet, do not record a failed attempt with backoff.
  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }
  if (!net_time_synced()) {
    return FetchResult::ClockUnset;
  }

  const time_t now = time(nullptr);
  struct tm tm_now = {};
  gmtime_r(&now, &tm_now);

  const uint32_t now_ms = millis();
  const uint16_t target_fetch_min = settings.tariff_fetch_time_min;

  // Process Import & Export Tariffs using common helper
  service_rate_target(imp_code, s_imp_state, s_rates.import_slots, s_rates.import_valid,
                      now_ms, now, tm_now, target_fetch_min, "import");
  service_rate_target(exp_code, s_exp_state, s_rates.export_slots, s_rates.export_valid,
                      now_ms, now, tm_now, target_fetch_min, "export");

  if (s_rates.import_valid || s_rates.export_valid) {
    s_last_result = FetchResult::Ok;
  } else if (!imp_code.isEmpty() &&
             s_imp_state.last_result != FetchResult::NotConfigured) {
    s_last_result = s_imp_state.last_result;
  } else if (!exp_code.isEmpty() &&
             s_exp_state.last_result != FetchResult::NotConfigured) {
    s_last_result = s_exp_state.last_result;
  } else {
    s_last_result = FetchResult::NotConfigured;
  }

  return s_last_result;
}

const DayTariffRates& tariff_rates_get() {
  return s_rates;
}

uint8_t tariff_rates_get_current_slot() {
  const time_t now = time(nullptr);
  if (!net_time_synced()) {
    return 0;
  }
  time_t day_start = 0;
  time_t day_end = 0;
  if (tariff_get_utc_day_bounds(now, &day_start, &day_end) && now >= day_start && now < day_end) {
    const uint8_t slot = static_cast<uint8_t>((now - day_start) / HALF_HOUR_SECONDS);
    return slot < TARIFF_SLOTS_PER_DAY ? slot : TARIFF_SLOTS_PER_DAY - 1;
  }

  return 0;
}

bool tariff_rates_get_current(float* out_import_p, float* out_export_p) {
  const uint8_t slot = tariff_rates_get_current_slot();
  bool found = false;

  if (out_import_p) {
    if (s_rates.import_valid && s_rates.import_slots[slot].valid) {
      *out_import_p = s_rates.import_slots[slot].pence;
      found = true;
    } else {
      *out_import_p = 0.0f;
    }
  }

  if (out_export_p) {
    if (s_rates.export_valid && s_rates.export_slots[slot].valid) {
      *out_export_p = s_rates.export_slots[slot].pence;
      found = true;
    } else {
      *out_export_p = 0.0f;
    }
  }

  return found;
}

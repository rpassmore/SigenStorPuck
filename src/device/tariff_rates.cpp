#include "tariff_rates.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "board_config.h"
#include "settings.h"

namespace {

constexpr uint32_t RETRY_MS = 15 * 60 * 1000;  // 15 mins retry on error
constexpr uint16_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 10000;

constexpr const char* USER_AGENT = "SigenStorPuck/" PUCK_FW_VERSION " (ESP32-S3)";

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

DayTariffRates s_rates;
uint32_t s_last_attempt_ms = 0;
FetchResult s_last_result = FetchResult::NotConfigured;
String s_last_import_code;
String s_last_export_code;
int s_last_fetch_day_of_year = -1;

bool clock_is_plausible() {
  return time(nullptr) > 1704067200;  // 1 Jan 2024
}

String build_tariff_url(const String& product_code) {
  if (product_code.startsWith("E-1R-") || product_code.startsWith("E-2R-")) {
    return "https://api.octopus.energy/v1/products/" + product_code + "/standard-unit-rates/";
  }
  // Standard product code like AGILE-24-10-01 -> assume Region G (London) or direct product path
  return "https://api.octopus.energy/v1/products/" + product_code +
         "/electricity-tariffs/E-1R-" + product_code + "-G/standard-unit-rates/";
}

FetchResult fetch_rates_for_code(const String& product_code, RateSlot slots[48]) {
  if (product_code.isEmpty()) {
    return FetchResult::NotConfigured;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }
  if (!clock_is_plausible()) {
    return FetchResult::ClockUnset;
  }

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start,
                      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(TOTAL_TIMEOUT_MS);
  http.setUserAgent(USER_AGENT);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url = build_tariff_url(product_code);
  if (!http.begin(tls, url)) {
    return FetchResult::TlsFailed;
  }
  http.addHeader("Accept", "application/json");

  int status = http.GET();
  if (status <= 0) {
    http.end();
    return FetchResult::TlsFailed;
  }
  if (status != HTTP_CODE_OK) {
    http.end();
    return FetchResult::HttpError;
  }

  const String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error != DeserializationError::Ok) {
    return FetchResult::BadPayload;
  }

  JsonArrayConst results = doc["results"];
  if (results.isNull()) {
    return FetchResult::BadPayload;
  }

  // Clear slots
  for (int i = 0; i < 48; ++i) {
    slots[i].valid = false;
    slots[i].pence = 0.0f;
  }

  time_t now = time(nullptr);
  struct tm local_now = {};
  localtime_r(&now, &local_now);

  int loaded_count = 0;
  for (JsonObjectConst entry : results) {
    const char* valid_from_str = entry["valid_from"];
    if (!valid_from_str) continue;

    // Parse ISO timestamp: "2026-09-19T00:30:00Z" or similar
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (sscanf(valid_from_str, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) < 5) {
      continue;
    }

    // Check if entry belongs to current local date
    if (year == (local_now.tm_year + 1900) && month == (local_now.tm_mon + 1) &&
        day == local_now.tm_mday) {
      int slot_idx = (hour * 2) + (minute >= 30 ? 1 : 0);
      if (slot_idx >= 0 && slot_idx < 48) {
        float val = entry["value_inc_vat"] | 0.0f;
        slots[slot_idx].pence = val;
        slots[slot_idx].valid = true;
        loaded_count++;
      }
    }
  }

  return (loaded_count > 0) ? FetchResult::Ok : FetchResult::BadPayload;
}

}  // namespace

FetchResult tariff_rates_service() {
  const Settings& settings = settings_get();
  const String& imp_code = settings.tariff_import_code;
  const String& exp_code = settings.tariff_export_code;

  if (imp_code.isEmpty() && exp_code.isEmpty()) {
    s_last_result = FetchResult::NotConfigured;
    return s_last_result;
  }

  time_t now = time(nullptr);
  struct tm tm_now = {};
  bool clock_ok = clock_is_plausible();
  if (clock_ok) {
    localtime_r(&now, &tm_now);
  }

  const uint32_t now_ms = millis();
  const uint16_t current_min = tm_now.tm_hour * 60 + tm_now.tm_min;
  const uint16_t target_fetch_min = settings.tariff_fetch_time_min;

  bool code_changed = (imp_code != s_last_import_code) || (exp_code != s_last_export_code);
  bool day_changed = (tm_now.tm_yday != s_last_fetch_day_of_year);
  bool fetch_due = false;

  if (code_changed || !s_rates.import_valid || !s_rates.export_valid) {
    fetch_due = true;
  } else if (clock_ok && day_changed && current_min >= target_fetch_min) {
    fetch_due = true;
  }

  const bool retrying_too_soon = s_last_result != FetchResult::Ok && s_last_attempt_ms != 0 &&
                                 (now_ms - s_last_attempt_ms < RETRY_MS);

  if (fetch_due && !retrying_too_soon) {
    s_last_attempt_ms = now_ms;
    s_last_import_code = imp_code;
    s_last_export_code = exp_code;

    FetchResult imp_res = FetchResult::Ok;
    FetchResult exp_res = FetchResult::Ok;

    if (!imp_code.isEmpty()) {
      imp_res = fetch_rates_for_code(imp_code, s_rates.import_slots);
      s_rates.import_valid = (imp_res == FetchResult::Ok);
    }
    if (!exp_code.isEmpty()) {
      exp_res = fetch_rates_for_code(exp_code, s_rates.export_slots);
      s_rates.export_valid = (exp_res == FetchResult::Ok);
    }

    if (s_rates.import_valid || s_rates.export_valid) {
      s_last_result = FetchResult::Ok;
      s_rates.fetched_time = now;
      if (clock_ok) {
        s_last_fetch_day_of_year = tm_now.tm_yday;
      }
      Serial.printf("[tariff_rates] successfully loaded day rates for import (%s) / export (%s)\n",
                    s_rates.import_valid ? "ok" : "n/a", s_rates.export_valid ? "ok" : "n/a");
    } else {
      s_last_result = (imp_res != FetchResult::Ok) ? imp_res : exp_res;
      Serial.printf("[tariff_rates] rate fetch failed: %s\n", fetch_result_name(s_last_result));
    }
  }

  return s_last_result;
}

const DayTariffRates& tariff_rates_get() {
  return s_rates;
}

uint8_t tariff_rates_get_current_slot() {
  time_t now = time(nullptr);
  struct tm tm_now = {};
  if (localtime_r(&now, &tm_now) != nullptr) {
    uint8_t slot = (tm_now.tm_hour * 2) + (tm_now.tm_min >= 30 ? 1 : 0);
    return slot < 48 ? slot : 47;
  }
  return 0;
}

bool tariff_rates_get_current(float* out_import_p, float* out_export_p) {
  uint8_t slot = tariff_rates_get_current_slot();
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

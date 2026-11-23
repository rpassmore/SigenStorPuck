#include "tariff_api.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include <algorithm>
#include <mutex>

#include "http_psram.h"
#include "net.h"
#include "psram_allocator.h"
#include "tariff_math.h"

namespace {

constexpr const char* HOST = "https://api.octopus.energy/v1/products/";

// Refresh products list every 24 hours
constexpr uint32_t REFRESH_MS = 24 * 60 * 60 * 1000;

// Retry interval on failure
constexpr uint32_t RETRY_MS = 60 * 1000;

constexpr uint16_t CONNECT_TIMEOUT_MS = 4000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 8000;
constexpr size_t PSRAM_HTTP_BUFFER_SIZE = 64 * 1024;

// A year of weekly samples was a worst case of 53 sequential HTTPS requests —
// around ten minutes of stalled polling when the API stopped answering. Product
// availability changes rarely, so a quarterly lookback is plenty; three failures
// or three empty weeks in a row means further requests are not worth the stall.
constexpr int WEEKS_COUNT = 12;
constexpr int MAX_CONSECUTIVE_FAILURES = 3;
constexpr int MAX_EMPTY_WEEKS = 3;

std::vector<TariffProduct> s_import_products;
std::vector<TariffProduct> s_export_products;
// The poll task replaces the vectors while the settings page iterates them on
// the LVGL thread. Readers get a copy taken under this lock, so a reallocation
// cannot pull the storage out from under an in-flight dropdown render.
std::mutex s_cache_lock;

uint32_t s_last_attempt_ms = 0;
FetchResult s_last_result = FetchResult::NotConfigured;
bool s_ready = false;

bool has_product_code(const std::vector<TariffProduct>& list, const char* code) {
  if (!code) return false;
  for (const auto& item : list) {
    if (item.code == code) {
      return true;
    }
  }
  return false;
}

FetchResult fetch_products() {
  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }
  if (!net_time_synced()) {
    return FetchResult::ClockUnset;
  }

  std::vector<TariffProduct> new_imports;
  std::vector<TariffProduct> new_exports;
  new_imports.reserve(50);
  new_exports.reserve(50);

  const time_t now = time(nullptr);
  constexpr time_t WEEK_SECONDS = 7 * 24 * 3600;

  bool any_http_success = false;
  FetchResult last_error = FetchResult::HttpError;
  int consecutive_failures = 0;
  int empty_weeks = 0;

  PsramBufferStream payload_buffer(PSRAM_HTTP_BUFFER_SIZE);
  if (!payload_buffer.isValid()) {
    return FetchResult::BadPayload;
  }

  for (int week = 0; week <= WEEKS_COUNT; ++week) {
    const time_t sample_time = now - static_cast<time_t>(week) * WEEK_SECONDS;
    char time_str[32];
    tariff_format_iso8601_utc(sample_time, time_str, sizeof(time_str));

    char url[128];
    snprintf(url, sizeof(url), "%s?available_at=%s", HOST, time_str);

    const FetchResult http_res = http_get_psram(url, payload_buffer,
                                                CONNECT_TIMEOUT_MS, TOTAL_TIMEOUT_MS);
    if (http_res != FetchResult::Ok) {
      last_error = http_res;
      ++consecutive_failures;
      delay(1);
      if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        break;
      }
      continue;
    }
    consecutive_failures = 0;

    PsramAllocator json_allocator;
    JsonDocument doc(&json_allocator);

    // Retain only fields of interest to minimize memory consumption. The filter
    // is a document too, so it needs the same PSRAM routing as the payload.
    JsonDocument filter(&json_allocator);
    JsonObject filter_prod = filter["results"].add<JsonObject>();
    filter_prod["code"] = true;
    filter_prod["full_name"] = true;
    filter_prod["direction"] = true;

    const DeserializationError error =
        deserializeJson(doc, payload_buffer.c_str(), DeserializationOption::Filter(filter));
    if (error != DeserializationError::Ok) {
      last_error = FetchResult::BadPayload;
      ++consecutive_failures;
      delay(1);
      if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        break;
      }
      continue;
    }

    JsonArrayConst results = doc["results"];
    if (results.isNull()) {
      last_error = FetchResult::BadPayload;
      ++consecutive_failures;
      delay(1);
      if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        break;
      }
      continue;
    }
    consecutive_failures = 0;

    any_http_success = true;

    for (JsonObjectConst prod : results) {
      const char* code = prod["code"];
      const char* name = prod["full_name"];
      const char* direction = prod["direction"];
      if (!code || !name) {
        continue;
      }

      const bool is_export = (direction && strcmp(direction, "EXPORT") == 0);
      std::vector<TariffProduct>& target_list = is_export ? new_exports : new_imports;

      if (!has_product_code(target_list, code)) {
        TariffProduct product;
        product.code = code;
        product.display_name = name;
        product.is_export = is_export;
        target_list.push_back(std::move(product));
      }
    }

    if (results.size() == 0) {
      ++empty_weeks;
    } else {
      empty_weeks = 0;
    }
    delay(1);
    if (empty_weeks >= MAX_EMPTY_WEEKS) {
      break;
    }
  }

  if (new_imports.empty() && new_exports.empty()) {
    return any_http_success ? FetchResult::BadPayload : last_error;
  }

  // Sort the products
  std::sort(new_imports.begin(), new_imports.end(), [](const TariffProduct& a, const TariffProduct& b) {
      return a.display_name < b.display_name;
  });
  std::sort(new_exports.begin(), new_exports.end(), [](const TariffProduct& a, const TariffProduct& b) {
      return a.display_name < b.display_name;
  });

  size_t import_count = 0;
  size_t export_count = 0;
  {
    const std::lock_guard<std::mutex> lock(s_cache_lock);
    s_import_products = std::move(new_imports);
    s_export_products = std::move(new_exports);
    s_ready = true;
    import_count = s_import_products.size();
    export_count = s_export_products.size();
  }
  Serial.printf("[tariff_api] loaded %u import and %u export products\n",
                static_cast<unsigned>(import_count), static_cast<unsigned>(export_count));

  return FetchResult::Ok;
}

}  // namespace

FetchResult tariff_api_service() {
  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }
  if (!net_time_synced()) {
    return FetchResult::ClockUnset;
  }

  const uint32_t now_ms = millis();
  FetchResult last_result;
  bool ready;
  {
    const std::lock_guard<std::mutex> lock(s_cache_lock);
    last_result = s_last_result;
    ready = s_ready;
  }
  const bool stale = s_last_attempt_ms == 0 || now_ms - s_last_attempt_ms >= REFRESH_MS;
  const bool retrying_too_soon = last_result != FetchResult::Ok && s_last_attempt_ms != 0 &&
                                 now_ms - s_last_attempt_ms < RETRY_MS;

  if ((!ready || stale) && !retrying_too_soon) {
    s_last_attempt_ms = now_ms;
    const FetchResult result = fetch_products();
    {
      const std::lock_guard<std::mutex> lock(s_cache_lock);
      s_last_result = result;
    }
    if (result != FetchResult::Ok) {
      Serial.printf("[tariff_api] product fetch failed: %s\n", fetch_result_name(result));
    }
    return result;
  }
  return last_result;
}

std::vector<TariffProduct> tariff_api_get_import_products() {
  const std::lock_guard<std::mutex> lock(s_cache_lock);
  return s_import_products;
}

std::vector<TariffProduct> tariff_api_get_export_products() {
  const std::lock_guard<std::mutex> lock(s_cache_lock);
  return s_export_products;
}

FetchResult tariff_api_last_result() {
  const std::lock_guard<std::mutex> lock(s_cache_lock);
  return s_last_result;
}

bool tariff_api_ready() {
  const std::lock_guard<std::mutex> lock(s_cache_lock);
  return s_ready;
}

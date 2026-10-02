#include "tariff_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <set>
#include <time.h>

#include "board_config.h"

namespace {

constexpr const char* HOST = "https://api.octopus.energy/v1/products/";

// Refresh products list every 24 hours
constexpr uint32_t REFRESH_MS = 24 * 60 * 60 * 1000;

// Retry interval on failure
constexpr uint32_t RETRY_MS = 15 * 60 * 1000;

constexpr uint16_t CONNECT_TIMEOUT_MS = 4000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 8000;

constexpr const char* USER_AGENT = "SigenStorPuck/" PUCK_FW_VERSION " (ESP32-S3)";

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

std::vector<TariffProduct> s_import_products;
std::vector<TariffProduct> s_export_products;

uint32_t s_last_attempt_ms = 0;
FetchResult s_last_result = FetchResult::NotConfigured;
bool s_ready = false;

bool clock_is_plausible() {
  return time(nullptr) > 1704067200;  // 1 Jan 2024
}

FetchResult fetch_products() {
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
  http.setReuse(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  std::vector<TariffProduct> new_imports;
  std::vector<TariffProduct> new_exports;
  std::set<String> seen_imports;
  std::set<String> seen_exports;

  const time_t now = time(nullptr);
  constexpr time_t WEEK_SECONDS = 7 * 24 * 3600;
  constexpr int WEEKS_COUNT = 52;

  bool any_http_success = false;
  FetchResult last_error = FetchResult::HttpError;

  for (int week = 0; week <= WEEKS_COUNT; ++week) {
    const time_t sample_time = now - static_cast<time_t>(week) * WEEK_SECONDS;
    struct tm tm_buf;
    gmtime_r(&sample_time, &tm_buf);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    const String url = String(HOST) + "?available_at=" + time_str;
    
    if (!http.begin(tls, url)) {
      last_error = FetchResult::TlsFailed;
      continue;
    }
    http.addHeader("Accept", "application/json");

    const int status = http.GET();
    if (status <= 0) {
      last_error = FetchResult::TlsFailed;
      http.end();
      delay(1);
      continue;
    }
    if (status != HTTP_CODE_OK) {
      last_error = FetchResult::HttpError;
      http.end();
      delay(1);
      continue;
    }

    const String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error != DeserializationError::Ok) {
      last_error = FetchResult::BadPayload;
      delay(1);
      continue;
    }

    JsonArrayConst results = doc["results"];
    if (results.isNull()) {
      last_error = FetchResult::BadPayload;
      delay(1);
      continue;
    }

    any_http_success = true;

    for (JsonObjectConst prod : results) {
      const char* code = prod["code"];
      const char* name = prod["full_name"];
      const char* direction = prod["direction"];
      if (!code || !name) {
        continue;
      }

      TariffProduct product;
      product.code = String(code);
      product.display_name = String(name);

      if (direction && strcmp(direction, "EXPORT") == 0) {
        product.is_export = true;
        if (seen_exports.insert(product.code).second) {
          new_exports.push_back(product);
        }
      } else {
        product.is_export = false;
        if (seen_imports.insert(product.code).second) {
          new_imports.push_back(product);
        }
      }
    }

    delay(1);
  }

  if (new_imports.empty() && new_exports.empty()) {
    return any_http_success ? FetchResult::BadPayload : last_error;
  }

  //sort the products
  std::sort(new_imports.begin(), new_imports.end(), [](const TariffProduct& a, const TariffProduct& b) {
      return a.display_name < b.display_name;
  });
  std::sort(new_exports.begin(), new_exports.end(), [](const TariffProduct& a, const TariffProduct& b) {
      return a.display_name < b.display_name;
  });


  s_import_products = std::move(new_imports);
  s_export_products = std::move(new_exports);
  s_ready = true;
  Serial.printf("[tariff_api] loaded %u import and %u export products\n",
                static_cast<unsigned>(s_import_products.size()),
                static_cast<unsigned>(s_export_products.size()));

  return FetchResult::Ok;
}

}  // namespace

FetchResult tariff_api_service() {
  const uint32_t now_ms = millis();
  const bool stale = s_last_attempt_ms == 0 || now_ms - s_last_attempt_ms >= REFRESH_MS;
  const bool retrying_too_soon = s_last_result != FetchResult::Ok && s_last_attempt_ms != 0 &&
                                 now_ms - s_last_attempt_ms < RETRY_MS;

  if ((!s_ready || stale) && !retrying_too_soon) {
    s_last_attempt_ms = now_ms;
    s_last_result = fetch_products();
    if (s_last_result != FetchResult::Ok) {
      Serial.printf("[tariff_api] product fetch failed: %s\n", fetch_result_name(s_last_result));
    }
  }
  return s_last_result;
}

const std::vector<TariffProduct>& tariff_api_get_import_products() {
  return s_import_products;
}

const std::vector<TariffProduct>& tariff_api_get_export_products() {
  return s_export_products;
}

FetchResult tariff_api_last_result() {
  return s_last_result;
}

bool tariff_api_ready() {
  return s_ready;
}

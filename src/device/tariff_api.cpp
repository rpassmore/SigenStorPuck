#include "tariff_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(tls, HOST)) {
    return FetchResult::TlsFailed;
  }
  http.addHeader("Accept", "application/json");

  const int status = http.GET();
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

  std::vector<TariffProduct> new_imports;
  std::vector<TariffProduct> new_exports;

  for (JsonObjectConst prod : results) {
    const char* code = prod["code"];
    const char* name = prod["display_name"];
    const char* direction = prod["direction"];
    if (!code || !name) {
      continue;
    }

    TariffProduct product;
    product.code = String(code);
    product.display_name = String(name);
    
    if (direction && strcmp(direction, "EXPORT") == 0) {
      product.is_export = true;
      new_exports.push_back(product);
    } else {
      product.is_export = false;
      new_imports.push_back(product);
    }
  }

  if (new_imports.empty() && new_exports.empty()) {
    return FetchResult::BadPayload;
  }

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

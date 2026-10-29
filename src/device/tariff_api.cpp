#include "tariff_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "board_config.h"

namespace {

constexpr const char* HOST = "https://api.octopus.energy/v1/products/";

// Refresh products list every 24 hours
constexpr uint32_t REFRESH_MS = 24 * 60 * 60 * 1000;

// Retry interval on failure
constexpr uint32_t RETRY_MS = 60 * 1000;

constexpr uint16_t CONNECT_TIMEOUT_MS = 4000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 8000;
constexpr size_t PSRAM_HTTP_BUFFER_SIZE = 64 * 1024;

constexpr const char* USER_AGENT = "SigenStorPuck/" PUCK_FW_VERSION " (ESP32-S3)";

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

// Custom ArduinoJson 7 allocator routing document allocations to PSRAM
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = nullptr;
    if (psramFound()) {
      ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return ptr ? ptr : malloc(size);
  }

  void deallocate(void* ptr) override {
    free(ptr);
  }

  void* reallocate(void* ptr, size_t new_size) override {
    void* p = nullptr;
    if (psramFound()) {
      p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return p ? p : realloc(ptr, new_size);
  }
};

// Stream sink buffering HTTP responses in PSRAM without internal SRAM allocations.
class PsramBufferStream : public Stream {
 public:
  explicit PsramBufferStream(size_t capacity) {
    if (psramFound()) {
      _buffer = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_buffer) {
      _buffer = static_cast<char*>(malloc(capacity));
    }
    _capacity = _buffer ? capacity : 0;
    if (_buffer) {
      _buffer[0] = '\0';
    }
  }

  ~PsramBufferStream() override {
    if (_buffer) {
      free(_buffer);
    }
  }

  size_t write(uint8_t b) override {
    if (!_buffer || _size + 1 >= _capacity) {
      return 0;
    }
    _buffer[_size++] = static_cast<char>(b);
    _buffer[_size] = '\0';
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!_buffer || size == 0) {
      return 0;
    }
    if (_size + size >= _capacity) {
      const size_t can_write = (_capacity > _size + 1) ? (_capacity - _size - 1) : 0;
      if (can_write > 0) {
        memcpy(_buffer + _size, buffer, can_write);
        _size += can_write;
        _buffer[_size] = '\0';
      }
      return can_write;
    }
    memcpy(_buffer + _size, buffer, size);
    _size += size;
    _buffer[_size] = '\0';
    return size;
  }

  const char* c_str() const { return _buffer ? _buffer : ""; }
  size_t length() const { return _size; }
  bool isValid() const { return _buffer != nullptr; }

  void clear() {
    _size = 0;
    if (_buffer) {
      _buffer[0] = '\0';
    }
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  char* _buffer = nullptr;
  size_t _capacity = 0;
  size_t _size = 0;
};

std::vector<TariffProduct> s_import_products;
std::vector<TariffProduct> s_export_products;

uint32_t s_last_attempt_ms = 0;
FetchResult s_last_result = FetchResult::NotConfigured;
bool s_ready = false;

bool clock_is_plausible() {
  return time(nullptr) > 1704067200;  // 1 Jan 2024
}

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
  if (!clock_is_plausible()) {
    return FetchResult::ClockUnset;
  }

  std::vector<TariffProduct> new_imports;
  std::vector<TariffProduct> new_exports;
  new_imports.reserve(50);
  new_exports.reserve(50);

  const time_t now = time(nullptr);
  constexpr time_t WEEK_SECONDS = 7 * 24 * 3600;
  constexpr int WEEKS_COUNT = 52;

  bool any_http_success = false;
  FetchResult last_error = FetchResult::HttpError;

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start,
                      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

  PsramBufferStream payload_buffer(PSRAM_HTTP_BUFFER_SIZE);
  if (!payload_buffer.isValid()) {
    return FetchResult::BadPayload;
  }

  for (int week = 0; week <= WEEKS_COUNT; ++week) {
    const time_t sample_time = now - static_cast<time_t>(week) * WEEK_SECONDS;
    struct tm tm_buf;
    gmtime_r(&sample_time, &tm_buf);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    char url[128];
    snprintf(url, sizeof(url), "%s?available_at=%s", HOST, time_str);

    tls.setCACertBundle(rootca_crt_bundle_start,
                        static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(TOTAL_TIMEOUT_MS);
    http.setUserAgent(USER_AGENT);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

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

    payload_buffer.clear();
    const int written = http.writeToStream(&payload_buffer);
    http.end();

    if (written < 0 || payload_buffer.length() == 0) {
      last_error = FetchResult::BadPayload;
      delay(1);
      continue;
    }

    PsramAllocator json_allocator;
    JsonDocument doc(&json_allocator);

    // Retain only fields of interest to minimize memory consumption
    JsonDocument filter;
    JsonObject filter_prod = filter["results"].add<JsonObject>();
    filter_prod["code"] = true;
    filter_prod["full_name"] = true;
    filter_prod["direction"] = true;

    const DeserializationError error =
        deserializeJson(doc, payload_buffer.c_str(), DeserializationOption::Filter(filter));
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
  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }
  if (!clock_is_plausible()) {
    return FetchResult::ClockUnset;
  }

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

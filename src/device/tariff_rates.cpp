#include "tariff_rates.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "board_config.h"
#include "poller.h"
#include "settings.h"

namespace {

constexpr uint32_t RETRY_MS = 60 * 1000;  // 1 min retry on error
constexpr uint16_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t TOTAL_TIMEOUT_MS = 10000;
constexpr int TARIFF_SLOT_COUNT = 48;
constexpr int64_t HALF_HOUR_SECONDS = 30 * 60;
constexpr uint16_t MAX_API_PAGES = 3;
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

// Stream sink that buffers HTTP responses directly into PSRAM without internal SRAM allocations.
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

bool clock_is_plausible() {
  return time(nullptr) > 1704067200;  // 1 Jan 2024
}

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
    struct tm tm_from = {};
    struct tm tm_to = {};
    gmtime_r(&day_start, &tm_from);
    gmtime_r(&day_end, &tm_to);
    snprintf(from_buf, sizeof(from_buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_from.tm_year + 1900, tm_from.tm_mon + 1, tm_from.tm_mday,
             tm_from.tm_hour, tm_from.tm_min, tm_from.tm_sec);
    snprintf(to_buf, sizeof(to_buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_to.tm_year + 1900, tm_to.tm_mon + 1, tm_to.tm_mday,
             tm_to.tm_hour, tm_to.tm_min, tm_to.tm_sec);

    const size_t cur_len = strlen(out_buf);
    if (cur_len < out_size) {
      snprintf(out_buf + cur_len, out_size - cur_len,
               "?period_from=%s&period_to=%s&page_size=100", from_buf, to_buf);
    }
  }

  return true;
}

void clear_slots(RateSlot slots[TARIFF_SLOT_COUNT]) {
  for (int i = 0; i < TARIFF_SLOT_COUNT; ++i) {
    slots[i].valid = false;
    slots[i].pence = 0.0f;
  }
}

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

bool parse_octopus_timestamp(const char* value, time_t* out) {
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

bool get_utc_day_bounds(time_t now, time_t* day_start, time_t* day_end) {
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

// Fill the fixed 48 local half-hour slots from an API interval.
void apply_interval(RateSlot slots[TARIFF_SLOT_COUNT],
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

  // Find every half-hour slot which overlaps the API interval. This is
  // intentionally based on overlap, rather than valid_from alone, so a
  // three-hour COSY/FIX interval fills all six corresponding slots.
  for (int slot = 0; slot < TARIFF_SLOT_COUNT; ++slot) {
    const time_t slot_start = day_start + static_cast<time_t>(slot * HALF_HOUR_SECONDS);
    const time_t slot_end = slot_start + static_cast<time_t>(HALF_HOUR_SECONDS);

    if (valid_from < slot_end && valid_to > slot_start) {
      slots[slot].pence = pence;
      slots[slot].valid = true;
    }
  }
}

FetchResult fetch_rates_for_code(const String& product_code, RateSlot slots[TARIFF_SLOT_COUNT]) {
  if (product_code.isEmpty()) {
    return FetchResult::NotConfigured;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return FetchResult::NoNetwork;
  }

  if (!clock_is_plausible()) {
    return FetchResult::ClockUnset;
  }

  const time_t now = time(nullptr);

  time_t day_start = 0;
  time_t day_end = 0;
  if (!get_utc_day_bounds(now, &day_start, &day_end)) {
    return FetchResult::BadPayload;
  }

  RateSlot temp_slots[TARIFF_SLOT_COUNT];
  clear_slots(temp_slots);

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start,
                    static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

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
    // due to a bug in the underlying network libs need to reattach the certs
    tls.setCACertBundle(rootca_crt_bundle_start,
      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(TOTAL_TIMEOUT_MS);
    http.setUserAgent(USER_AGENT);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept", "application/json");

    Serial.printf("url %s\n", url);
    if (!http.begin(tls, url)) {
      return FetchResult::TlsFailed;
    }

    const int status = http.GET();
    if (status <= 0) {
      http.end();
      return FetchResult::TlsFailed;
    }

    if (status != HTTP_CODE_OK) {
      http.end();
      return FetchResult::HttpError;
    }

    payload_buffer.clear();
    const int written = http.writeToStream(&payload_buffer);
    http.end();

    if (written < 0 || payload_buffer.length() == 0) {
      return FetchResult::BadPayload;
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

      if (!parse_octopus_timestamp(valid_from_str, &valid_from)) {
        continue;
      }

      if (valid_to_str != nullptr) {
        if (!parse_octopus_timestamp(valid_to_str, &valid_to)) {
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

      apply_interval(temp_slots, day_start, day_end,
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
  for (int i = 0; i < TARIFF_SLOT_COUNT; ++i) {
    if (temp_slots[i].valid) {
      memcpy(slots, temp_slots, sizeof(temp_slots));
      return FetchResult::Ok;
    }
  }

  return FetchResult::BadPayload;
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
  if (!clock_is_plausible()) {
    return FetchResult::ClockUnset;
  }

  const time_t now = time(nullptr);
  struct tm tm_now = {};
  gmtime_r(&now, &tm_now);

  const uint32_t now_ms = millis();
  const uint16_t current_min =
      static_cast<uint16_t>(tm_now.tm_hour * 60 + tm_now.tm_min);
  const uint16_t target_fetch_min = settings.tariff_fetch_time_min;

  // Process Import Tariff
  if (imp_code.isEmpty()) {
    if (s_rates.import_valid || s_imp_state.code[0] != '\0') {
      clear_slots(s_rates.import_slots);
      s_rates.import_valid = false;
      s_imp_state = {};
    }
  } else {
    const bool code_changed = (strcmp(imp_code.c_str(), s_imp_state.code) != 0);
    if (code_changed) {
      strncpy(s_imp_state.code, imp_code.c_str(), sizeof(s_imp_state.code) - 1);
      s_imp_state.code[sizeof(s_imp_state.code) - 1] = '\0';
      s_imp_state.last_attempt_ms = 0;
      s_imp_state.last_result = FetchResult::NotConfigured;
      s_imp_state.last_fetch_day_of_year = -1;
      s_imp_state.last_fetch_min_of_day = -1;
      s_rates.import_valid = false;
      clear_slots(s_rates.import_slots);
    }

    const bool day_changed =
        (s_imp_state.last_fetch_day_of_year != -1 &&
         tm_now.tm_yday != s_imp_state.last_fetch_day_of_year);
    if (day_changed) {
      s_imp_state.last_fetch_day_of_year = -1;
      s_imp_state.last_fetch_min_of_day = -1;
      s_rates.import_valid = false;
      clear_slots(s_rates.import_slots);
    }

    const bool fetch_due =
        code_changed || day_changed || !s_rates.import_valid ||
        (current_min >= target_fetch_min &&
         s_imp_state.last_fetch_min_of_day < target_fetch_min);

    const bool retrying_too_soon =
        (s_imp_state.last_result != FetchResult::Ok &&
         s_imp_state.last_attempt_ms != 0 &&
         (now_ms - s_imp_state.last_attempt_ms < RETRY_MS));

    if (fetch_due && !retrying_too_soon) {
      s_imp_state.last_attempt_ms = now_ms;
      s_imp_state.last_result =
          fetch_rates_for_code(imp_code, s_rates.import_slots);
      s_rates.import_valid = (s_imp_state.last_result == FetchResult::Ok);
      if (s_rates.import_valid) {
        s_imp_state.last_fetch_day_of_year = tm_now.tm_yday;
        s_imp_state.last_fetch_min_of_day = current_min;
        s_rates.fetched_time = static_cast<uint32_t>(now);
        Serial.printf(
            "[tariff_rates] successfully loaded day rates for import (%s)\n",
            imp_code.c_str());
      } else {
        Serial.printf("[tariff_rates] import rate fetch failed (%s): %s\n",
                      imp_code.c_str(),
                      fetch_result_name(s_imp_state.last_result));
      }
    }
  }

  // Process Export Tariff
  if (exp_code.isEmpty()) {
    if (s_rates.export_valid || s_exp_state.code[0] != '\0') {
      clear_slots(s_rates.export_slots);
      s_rates.export_valid = false;
      s_exp_state = {};
    }
  } else {
    const bool code_changed = (strcmp(exp_code.c_str(), s_exp_state.code) != 0);
    if (code_changed) {
      strncpy(s_exp_state.code, exp_code.c_str(), sizeof(s_exp_state.code) - 1);
      s_exp_state.code[sizeof(s_exp_state.code) - 1] = '\0';
      s_exp_state.last_attempt_ms = 0;
      s_exp_state.last_result = FetchResult::NotConfigured;
      s_exp_state.last_fetch_day_of_year = -1;
      s_exp_state.last_fetch_min_of_day = -1;
      s_rates.export_valid = false;
      clear_slots(s_rates.export_slots);
    }

    const bool day_changed =
        (s_exp_state.last_fetch_day_of_year != -1 &&
         tm_now.tm_yday != s_exp_state.last_fetch_day_of_year);
    if (day_changed) {
      s_exp_state.last_fetch_day_of_year = -1;
      s_exp_state.last_fetch_min_of_day = -1;
      s_rates.export_valid = false;
      clear_slots(s_rates.export_slots);
    }

    const bool fetch_due =
        code_changed || day_changed || !s_rates.export_valid ||
        (current_min >= target_fetch_min &&
         s_exp_state.last_fetch_min_of_day < target_fetch_min);

    const bool retrying_too_soon =
        (s_exp_state.last_result != FetchResult::Ok &&
         s_exp_state.last_attempt_ms != 0 &&
         (now_ms - s_exp_state.last_attempt_ms < RETRY_MS));

    if (fetch_due && !retrying_too_soon) {
      s_exp_state.last_attempt_ms = now_ms;
      s_exp_state.last_result =
          fetch_rates_for_code(exp_code, s_rates.export_slots);
      s_rates.export_valid = (s_exp_state.last_result == FetchResult::Ok);
      if (s_rates.export_valid) {
        s_exp_state.last_fetch_day_of_year = tm_now.tm_yday;
        s_exp_state.last_fetch_min_of_day = current_min;
        s_rates.fetched_time = static_cast<uint32_t>(now);
        Serial.printf(
            "[tariff_rates] successfully loaded day rates for export (%s)\n",
            exp_code.c_str());
      } else {
        Serial.printf("[tariff_rates] export rate fetch failed (%s): %s\n",
                      exp_code.c_str(),
                      fetch_result_name(s_exp_state.last_result));
      }
    }
  }

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
  if (!clock_is_plausible()) {
    return 0;
  }
  time_t day_start = 0;
  time_t day_end = 0;
  if (get_utc_day_bounds(now, &day_start, &day_end) && now >= day_start && now < day_end) {
    const uint8_t slot = static_cast<uint8_t>((now - day_start) / HALF_HOUR_SECONDS);
    return slot < TARIFF_SLOT_COUNT ? slot : TARIFF_SLOT_COUNT - 1;
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
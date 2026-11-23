#include "http_psram.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <atomic>

#include "board_config.h"
#include "http_deadline.h"

namespace {

constexpr const char* USER_AGENT = "SigenStorPuck/" PUCK_FW_VERSION " (ESP32-S3)";

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

}  // namespace

extern "C" void psram_allocator_note_fallback(size_t size) {
  static std::atomic<uint32_t> s_fallbacks{0};
  const uint32_t count = ++s_fallbacks;
  if (count == 1 || count % 100 == 0) {
    Serial.printf("[psram] allocation fell back to internal heap %u time(s), last %u B\n",
                  static_cast<unsigned>(count), static_cast<unsigned>(size));
  }
}

FetchResult http_get_psram(const char* url,
                           PsramBufferStream& out_buffer,
                           uint16_t connect_timeout_ms,
                           uint16_t total_timeout_ms) {
  if (!url || url[0] == '\0' || !out_buffer.isValid()) {
    return FetchResult::BadPayload;
  }

  DeadlineClient<WiFiClientSecure> tls;
  // Due to a bug in the underlying network libs need to reattach the certs
  tls.setCACertBundle(rootca_crt_bundle_start,
                      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

  // HTTPClient's socket timeout is not enough for NetworkClientSecure. Stop the
  // client at the wall-clock deadline so a mid-body stall cannot hold the poller.
  HttpDeadline deadline(tls, total_timeout_ms);
  tls.setDeadline(&deadline);

  HTTPClient http;
  http.setConnectTimeout(connect_timeout_ms);
  http.setTimeout(total_timeout_ms);
  http.setUserAgent(USER_AGENT);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(tls, url)) {
    return deadline.cancel() ? FetchResult::ReadTimeout : FetchResult::TlsFailed;
  }
  http.addHeader("Accept", "application/json");

  const int status = http.GET();
  if (status <= 0) {
    const bool expired = deadline.cancel();
    http.end();
    return expired ? FetchResult::ReadTimeout : FetchResult::TlsFailed;
  }
  if (status != HTTP_CODE_OK) {
    deadline.cancel();
    http.end();
    return FetchResult::HttpError;
  }

  out_buffer.clear();
  const int expected = http.getSize();
  const int written = http.writeToStream(&out_buffer);
  // Drain before end(): otherwise a late callback could shut down an fd that
  // end() has already closed and the OS has reused.
  const bool expired = deadline.cancel();
  http.end();

  if (expired) {
    Serial.println("[http] response deadline expired");
    return FetchResult::ReadTimeout;
  }
  if (written < 0 || out_buffer.length() == 0 || out_buffer.overflow()) {
    Serial.printf("[http] response overflow or empty (capacity %u bytes, received %u)\n",
                  static_cast<unsigned>(out_buffer.capacity()),
                  static_cast<unsigned>(out_buffer.length()));
    return FetchResult::BadPayload;
  }
  if (expected > 0 && static_cast<size_t>(expected) != out_buffer.length()) {
    Serial.printf("[http] short response: expected %d, got %u\n", expected,
                  static_cast<unsigned>(out_buffer.length()));
    return FetchResult::BadPayload;
  }

  return FetchResult::Ok;
}

#include "http_psram.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "board_config.h"

namespace {

constexpr const char* USER_AGENT = "SigenStorPuck/" PUCK_FW_VERSION " (ESP32-S3)";

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

}  // namespace

FetchResult http_get_psram(const char* url,
                           PsramBufferStream& out_buffer,
                           uint16_t connect_timeout_ms,
                           uint16_t total_timeout_ms) {
  if (!url || url[0] == '\0' || !out_buffer.isValid()) {
    return FetchResult::BadPayload;
  }

  WiFiClientSecure tls;
  // Due to a bug in the underlying network libs need to reattach the certs
  tls.setCACertBundle(rootca_crt_bundle_start,
                      static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));

  HTTPClient http;
  http.setConnectTimeout(connect_timeout_ms);
  http.setTimeout(total_timeout_ms);
  http.setUserAgent(USER_AGENT);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(tls, url)) {
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

  out_buffer.clear();
  const int written = http.writeToStream(&out_buffer);
  http.end();

  if (written < 0 || out_buffer.length() == 0) {
    return FetchResult::BadPayload;
  }

  return FetchResult::Ok;
}

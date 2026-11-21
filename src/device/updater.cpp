#include "updater.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "board_config.h"
#include "http_deadline.h"
#include "http_psram.h"
#include "net.h"
#include "psram_allocator.h"
#include "settings.h"

namespace {

// GitHub Pages, not the release assets.
//
// This repository is private, and `releases/latest/download/...` on a private
// repository needs an authenticated request — an anonymous device gets a flat 404,
// which is exactly what a fielded Puck reported. The alternative would be putting
// a GitHub token on every device, which is a real credential guarding far more
// than firmware, to read a file that is already published.
//
// Pages is public and already serves both files, because the installer needs them
// on one origin anyway (GitHub's release assets send no CORS header, so the web
// flasher could not fetch them cross-origin either). The device and the installer
// now read the same manifest from the same place.
//
// One consequence worth knowing: Pages deploys from main, so what a device sees as
// "the latest release" is whatever version main advertises, not the newest tag.
// The version bump is the gate — pushing code to main without bumping
// PUCK_FW_VERSION publishes nothing a device will act on.
//
// Redirects are still followed: Pages serves these through a CDN.
//
// Per-board: each variant fetches its own slug's manifest and image
// (SigenStorPuck/<slug>/...), so a 2.41 never pulls a 1.75 build. The two share a
// version, so the URL — built from the compile-time PUCK_BOARD_SLUG — is the
// discriminator, backed up by the `board` field check below.
constexpr const char* MANIFEST_URL =
    "https://markab.github.io/SigenStorPuck/" PUCK_BOARD_SLUG "/manifest.json";
constexpr const char* FIRMWARE_URL =
    "https://markab.github.io/SigenStorPuck/" PUCK_BOARD_SLUG "/firmware.bin";

// Opening the settings page starts a check, and saving any form re-renders it, so
// without a floor a few minutes of fiddling with settings would be a few dozen
// requests to GitHub. The "Check now" button bypasses this.
constexpr uint32_t CHECK_MIN_INTERVAL_MS = 10 * 60 * 1000;

// Manifest and firmware are separate TLS sessions. Both need an explicit
// deadline: HTTPClient's socket timeout is not reliable for NetworkClientSecure.
constexpr uint16_t MANIFEST_CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t MANIFEST_TOTAL_TIMEOUT_MS = 10000;
constexpr uint16_t FIRMWARE_TOTAL_TIMEOUT_MS = 60000;
constexpr size_t MAX_MANIFEST_BYTES = 16 * 1024;


// Guards s_status, which the background check writes and the settings page reads.
// Without it the page could copy a String mid-assignment, which is a use-after-free
// rather than merely a stale reading.
SemaphoreHandle_t s_lock = nullptr;
UpdateStatus s_status;

// Set by the settings page, consumed by the poll task. Nothing else coordinates
// them: a stale read either runs one extra check or defers it by one poll, and
// neither matters.
volatile bool s_check_pending = false;
volatile bool s_apply_pending = false;
uint32_t s_last_check_ms = 0;
bool s_ever_checked = false;

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

template <typename Client>
void attach_bundle(Client& client) {
  // The same validated-TLS rule as the API client: the cert bundle covers GitHub's
  // CDN as well as your own server, which is the reason for using a bundle rather
  // than pinning a root.
  client.setCACertBundle(rootca_crt_bundle_start,
                         static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
}

// Compares dotted versions numerically. A string compare would call "0.10.0" older
// than "0.9.0", which is exactly the point at which it would matter.
bool is_newer(const String& candidate, const String& current) {
  int ci = 0;
  int ni = 0;
  for (int part = 0; part < 3; ++part) {
    int c = 0;
    int n = 0;
    while (ci < static_cast<int>(candidate.length()) && isdigit(candidate[ci])) {
      c = c * 10 + (candidate[ci++] - '0');
    }
    while (ni < static_cast<int>(current.length()) && isdigit(current[ni])) {
      n = n * 10 + (current[ni++] - '0');
    }
    if (c != n) {
      return c > n;
    }
    if (ci < static_cast<int>(candidate.length())) ++ci;  // skip the dot
    if (ni < static_cast<int>(current.length())) ++ni;
  }
  return false;
}

// HTTPClient returns its own negative codes alongside real HTTP statuses, so a
// bare number is not enough to report. "manifest HTTP -1" told a user nothing
// except that something had gone wrong — and -1 is not an HTTP status at all, it
// is a connection that never opened.
//
// The free heap goes in the connection-failure cases on purpose. A TLS handshake
// against this cert bundle needs tens of kilobytes in one piece, and running out
// is a leading cause of a refused connection that looks like a network fault.
String describe_http_error(int code) {
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED:
      return String("could not open a TLS connection (free heap ") + ESP.getFreeHeap() + " B)";
    case HTTPC_ERROR_CONNECTION_LOST:
      return "the connection dropped mid-request";
    case HTTPC_ERROR_NOT_CONNECTED:
      return "not connected";
    case HTTPC_ERROR_SEND_HEADER_FAILED:
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
      return "could not send the request";
    case HTTPC_ERROR_NO_HTTP_SERVER:
      return "no HTTP server answered";
    case HTTPC_ERROR_TOO_LESS_RAM:
      return String("not enough memory (free heap ") + ESP.getFreeHeap() + " B)";
    case HTTPC_ERROR_READ_TIMEOUT:
      return "GitHub did not answer in time";
    case HTTPC_ERROR_ENCODING:
      return "the reply used an encoding we cannot read";
    default:
      break;
  }
  if (code < 0) {
    return String("connection failed (") + code + ")";
  }
  return String("GitHub answered HTTP ") + code;
}

void publish(const UpdateStatus& status) {
  if (s_lock == nullptr) {
    s_status = status;
    return;
  }
  if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    s_status = status;
    xSemaphoreGive(s_lock);
  }
}

struct ManifestInfo {
  String version;
  uint32_t firmware_size = 0;
};

// Shared by the periodic check and the re-check immediately before flashing.
// A manifest that changed between the two must not be able to turn an "apply"
// into a downgrade or an image that does not fit the spare slot.
bool fetch_manifest(ManifestInfo* info, String* error) {
  DeadlineClient<WiFiClientSecure> client;
  attach_bundle(client);
  HttpDeadline deadline(client, MANIFEST_TOTAL_TIMEOUT_MS);
  client.setDeadline(&deadline);

  HTTPClient http;
  http.setConnectTimeout(MANIFEST_CONNECT_TIMEOUT_MS);
  http.setTimeout(MANIFEST_TOTAL_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, MANIFEST_URL)) {
    *error = "could not parse the update URL";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    const bool expired = deadline.cancel();
    *error = expired ? "the update server stalled" : describe_http_error(code);
    http.end();
    return false;
  }

  LimitedStringStream stream(MAX_MANIFEST_BYTES);
  const int written = http.writeToStream(&stream);
  const bool expired = deadline.cancel();
  http.end();
  if (expired) {
    *error = "the update server stalled";
    return false;
  }
  if (written < 0 || stream.overflow() || stream.length() == 0) {
    *error = "manifest response was empty or too large";
    return false;
  }

  PsramAllocator json_allocator;
  JsonDocument doc(&json_allocator);
  if (deserializeJson(doc, stream.value()) != DeserializationError::Ok) {
    *error = "manifest did not parse";
    return false;
  }

  info->version = doc["version"] | "";
  if (info->version.isEmpty()) {
    *error = "manifest has no version";
    return false;
  }
  info->firmware_size = doc["firmware_size"] | 0u;

  // Cross-flash guard: never act on a manifest built for another board. The
  // per-slug URL already keeps them apart; this catches a misplaced manifest,
  // which matters because the two variants share a version and so cannot be told
  // apart by version alone. An absent board field is allowed (older manifests).
  const String board = doc["board"] | "";
  if (!board.isEmpty() && board != PUCK_BOARD_SLUG) {
    *error = "the release is for a different board";
    return false;
  }
  return true;
}

bool firmware_fits(uint32_t size, String* error) {
  if (size == 0) {
    return true;  // older manifests do not advertise a size
  }
  const esp_partition_t* ota = esp_ota_get_next_update_partition(nullptr);
  if (ota == nullptr) {
    *error = "no spare OTA partition is available";
    return false;
  }
  if (size > ota->size) {
    *error = "the firmware image does not fit the spare OTA slot";
    return false;
  }
  return true;
}

// Called from loop() once the image has had time to prove itself. Confirming in
// updater_begin() would accept any image that merely drew one frame, even if it
// crashed later in the poller or settings path. Until this runs, the bootloader
// still rolls back on the next reboot.
bool confirm_pending_update() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    Serial.println("[update] cannot identify the running app partition");
    return false;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result = esp_ota_get_state_partition(running, &state);
  if (state_result != ESP_OK) {
    Serial.printf("[update] rollback state unavailable (%d)\n", static_cast<int>(state_result));
    return false;
  }
  Serial.printf("[update] boot partition %s at 0x%x, image state %d\n", running->label,
                static_cast<unsigned>(running->address), static_cast<int>(state));
  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    return true;  // nothing to confirm
  }

  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  Serial.printf("[update] confirmed booted firmware (%s)\n",
                err == ESP_OK ? "ok" : esp_err_to_name(err));
  return err == ESP_OK;
}

// Builds a status rather than mutating the shared one, so the whole result becomes
// visible in a single assignment and the page never sees a half-updated check.
UpdateStatus perform_check() {
  UpdateStatus result;

  if (WiFi.status() != WL_CONNECTED) {
    result.state = UpdateState::Failed;
    result.message = "no network";
    return result;
  }

  // The same gate sigen_api.cpp has, and for the same reason: a certificate
  // cannot be validated against a clock that has not been set, so the handshake
  // fails and reports itself as a refused connection rather than as the clock
  // problem it is.
  //
  // This became much easier to hit when checks started firing as the settings
  // page opens. A deliberate button press happened minutes after boot; a page
  // load happens seconds after it, which is a race against NTP.
  if (!net_time_synced()) {
    result.state = UpdateState::Failed;
    result.message = "waiting for the clock — HTTPS cannot be validated yet";
    return result;
  }

  ManifestInfo info;
  String error;
  if (!fetch_manifest(&info, &error)) {
    result.state = UpdateState::Failed;
    result.message = error;
    return result;
  }

  result.latest_version = info.version;
  result.firmware_size = info.firmware_size;
  if (!firmware_fits(info.firmware_size, &error)) {
    result.state = UpdateState::Failed;
    result.message = error;
    return result;
  }

  const bool newer = is_newer(result.latest_version, String(PUCK_FW_VERSION));
  result.state = newer ? UpdateState::Available : UpdateState::UpToDate;
  result.message = newer ? "" : "This is the latest release.";
  Serial.printf("[update] latest %s, running %s: %s\n", result.latest_version.c_str(),
                PUCK_FW_VERSION, newer ? "newer available" : "up to date");
  return result;
}


void perform_apply() {
  if (updater_status().state != UpdateState::Available) {
    return;
  }
  UpdateStatus status = updater_status();

  // Re-read the manifest before touching a slot. The check may be minutes old,
  // and a changed version or an oversized image turns "apply" into a downgrade
  // or a failed flash at exactly the wrong moment.
  ManifestInfo info;
  String error;
  if (!fetch_manifest(&info, &error)) {
    status.state = UpdateState::Failed;
    status.message = error;
    publish(status);
    return;
  }
  if (!is_newer(info.version, String(PUCK_FW_VERSION)) || info.version != status.latest_version) {
    status.state = UpdateState::Failed;
    status.message = "the release changed since the check — check again";
    publish(status);
    return;
  }
  if (!firmware_fits(info.firmware_size, &error)) {
    status.state = UpdateState::Failed;
    status.message = error;
    publish(status);
    return;
  }

  Serial.printf("[update] downloading %s\n", info.version.c_str());
  status.state = UpdateState::Downloading;
  status.message = "";
  status.latest_version = info.version;
  status.firmware_size = info.firmware_size;
  publish(status);

  DeadlineClient<WiFiClientSecure> client;
  attach_bundle(client);
  HttpDeadline deadline(client, FIRMWARE_TOTAL_TIMEOUT_MS);
  client.setDeadline(&deadline);

  // A local HTTPUpdate instance carries its own timeout; the global one has no
  // explicit deadline. The request callback applies the timeout to the actual
  // HTTPClient after redirects have been resolved.
  HTTPUpdate downloader(FIRMWARE_TOTAL_TIMEOUT_MS);
  downloader.rebootOnUpdate(true);
  downloader.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Writes into whichever OTA slot is not running. If the new image will not boot,
  // the bootloader falls back to this one — which is the reason for the stock
  // two-slot 16 MB partition table.
  const t_httpUpdate_return result =
      downloader.update(client, FIRMWARE_URL, "",
                        [](HTTPClient* http) {
                          http->setConnectTimeout(MANIFEST_CONNECT_TIMEOUT_MS);
                          http->setTimeout(FIRMWARE_TOTAL_TIMEOUT_MS);
                        });
  const bool expired = deadline.cancel();
  switch (result) {
    case HTTP_UPDATE_OK:
      // Not reached: rebootOnUpdate restarts us.
      status.state = UpdateState::UpToDate;
      break;
    case HTTP_UPDATE_NO_UPDATES:
      status.state = UpdateState::UpToDate;
      status.message = "the release had no update to give";
      break;
    case HTTP_UPDATE_FAILED:
    default:
      status.state = UpdateState::Failed;
      status.message = expired ? "the update download timed out"
                               : downloader.getLastErrorString();
      Serial.printf("[update] failed: %s\n", status.message.c_str());
      break;
  }
  publish(status);
}

}  // namespace

void updater_begin() {
  if (s_lock == nullptr) {
    s_lock = xSemaphoreCreateMutex();
  }
  UpdateStatus initial;
  initial.state = UpdateState::Idle;
  publish(initial);
}

bool updater_confirm_pending_update() {
  return confirm_pending_update();
}

bool updater_enabled() {
  return settings_snapshot().check_updates;
}

void updater_request_check(bool force) {
  // The automatic check (page open) is opt-in; the button is not, because
  // pressing it is the asking. It used to bypass this by checking synchronously
  // from the page handler instead — on the LVGL thread, and possibly alongside the
  // poll task's own TLS session — which froze the screen for the round trip and
  // could fail the handshake for want of a second session's heap.
  if ((!updater_enabled() && !force) || s_check_pending) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!force && s_ever_checked && (millis() - s_last_check_ms) < CHECK_MIN_INTERVAL_MS) {
    return;
  }

  s_check_pending = true;
  UpdateStatus checking = updater_status();
  checking.state = UpdateState::Checking;
  checking.message = "";
  publish(checking);
}

void updater_request_apply() {
  if (updater_status().state == UpdateState::Available) {
    s_apply_pending = true;
  }
}

void updater_service() {
  // Apply first. It ends in a reboot, so anything queued behind it is moot.
  if (s_apply_pending) {
    s_apply_pending = false;
    perform_apply();
    return;
  }
  if (!s_check_pending) {
    return;
  }
  s_check_pending = false;
  publish(perform_check());
  s_last_check_ms = millis();
  s_ever_checked = true;
}

UpdateStatus updater_status() {
  UpdateStatus copy;
  if (s_lock != nullptr && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    copy = s_status;
    xSemaphoreGive(s_lock);
  }
  return copy;
}

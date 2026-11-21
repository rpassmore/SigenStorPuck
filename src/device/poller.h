// The polling loop, on its own task.
//
// PLAN.md §B3: one network task pinned to core 0 while Arduino's loop() runs LVGL
// on core 1. A single mutex guards the shared snapshot and is held only for the
// swap — rendering happens outside the lock, so the UI never stalls on the
// network.

#pragma once

#include <Arduino.h>

#include "sigen_api.h"
#include "snapshot.h"

struct PollStatus {
  FetchResult last_result = FetchResult::NotConfigured;
  int last_http_status = 0;
  uint32_t consecutive_failures = 0;
  // millis() of the last successful fetch, or 0 if there has never been one.
  uint32_t last_ok_ms = 0;
  bool ever_succeeded = false;
};

// Connection tests from the settings page. The request is queued here and the
// fetch runs on the poll task, like the update check: a second TLS session from
// the LVGL thread would both freeze the display and risk exhausting the heap
// alongside the poller's own client.
enum class ConnectionTestKind : uint8_t { None, Server, HomeAssistant };

struct ConnectionTestResult {
  ConnectionTestKind kind = ConnectionTestKind::None;
  bool pending = false;
  bool running = false;
  bool done = false;
  FetchResult result = FetchResult::NotConfigured;
  int status_code = 0;
  // Server test details.
  uint32_t age_s = 0;
  bool home_known = false;
  float home_kw = 0.0f;
  // Home Assistant test details.
  size_t available = 0;
  size_t configured = 0;
  size_t unavailable = 0;
  size_t invalid_number = 0;
  size_t unsupported_unit = 0;
};

// Returns immediately. Duplicate requests while one is pending are ignored.
void poller_request_connection_test(ConnectionTestKind kind);

// Called from the poll task only; runs a queued test, if any.
void poller_service_connection_tests();

// Mutex-protected copy for the settings page to render.
ConnectionTestResult poller_connection_test_status();

// Starts the task. Safe to call before the device is provisioned: it will idle
// and pick the settings up as soon as they are saved.
void poller_begin();

// Copies the last good snapshot out under the lock. Returns false if there has
// never been a successful fetch, in which case `out` is untouched. When supplied,
// `generation` changes on each successful publish so the UI can skip an unchanged
// application-data update without coupling LVGL to the network task.
bool poller_snapshot(Snapshot* out, uint32_t* generation = nullptr);

// The past day the buttons have stepped back to, if one is loaded. False at an
// offset of zero, and while the fetch for a newly-chosen day is still in flight
// — the caller keeps showing live rather than a day it has not got yet. Its
// optional generation changes when a dated snapshot is published or cleared.
bool poller_day_snapshot(Snapshot* out, uint32_t* generation = nullptr);

PollStatus poller_status();

// Minutes east of UTC, latched from the last reading that carried one; 0 until
// then, which is also a legitimate value, so this cannot distinguish "UTC" from
// "not known yet" and does not try to.
//
// The device's own clock is deliberately UTC (net.cpp asks NTP for no timezone),
// so this is what anything needing a local time has to go through: the day
// charts' midnight and the screen-off schedule both.
int32_t poller_tz_offset_min();

// Asks the loop to fetch immediately rather than waiting out its interval — used
// after the settings change, so a corrected URL takes effect at once.
void poller_wake();

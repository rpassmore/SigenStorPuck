#include "puck_lv_mem.h"

#include <Arduino.h>

#include <atomic>

extern "C" void puck_lv_mem_note_fallback(const char* operation, size_t size) {
  static std::atomic<uint32_t> s_fallbacks{0};
  const uint32_t count = ++s_fallbacks;
  // Log the first event and every hundredth after that. A one-off failure during
  // boot matters; a thousand identical lines would only hide it.
  if (count == 1 || count % 100 == 0) {
    Serial.printf("[lvgl] PSRAM %s of %u B fell back to internal heap %u time(s)\n",
                  operation, static_cast<unsigned>(size), static_cast<unsigned>(count));
  }
}

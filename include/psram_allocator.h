// ArduinoJson allocator that keeps per-poll documents out of the internal heap.
//
// The device build routes every parse through PSRAM: JSON trees are short-lived,
// variable-sized and frequent, while mbedTLS needs large contiguous internal
// blocks for a handshake. Leaving the default allocator in place on even one
// poll path reintroduces the fragmentation this was written to avoid.
//
// The simulator has no ESP-IDF heap capabilities, so it uses plain malloc there;
// the parsing code itself is identical in both builds.

#pragma once

#include <ArduinoJson.h>
#include <stdlib.h>

#ifdef ARDUINO
#include <esp_heap_caps.h>

// Defined in src/device/http_psram.cpp. Counted rather than printed on every
// allocation: a PSRAM exhaustion event must be visible without flooding serial.
extern "C" void psram_allocator_note_fallback(size_t size);
#endif

struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = nullptr;
#ifdef ARDUINO
    if (psramFound()) {
      ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (ptr == nullptr) {
      psram_allocator_note_fallback(size);
    }
#endif
    return ptr != nullptr ? ptr : malloc(size);
  }

  void deallocate(void* ptr) override {
    free(ptr);
  }

  void* reallocate(void* ptr, size_t new_size) override {
    void* grown = nullptr;
#ifdef ARDUINO
    if (psramFound()) {
      grown = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (grown == nullptr) {
      psram_allocator_note_fallback(new_size);
    }
#endif
    return grown != nullptr ? grown : realloc(ptr, new_size);
  }
};

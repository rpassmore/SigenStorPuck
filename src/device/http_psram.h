#pragma once

#include <ArduinoJson.h>
#include <Stream.h>
#include <esp_heap_caps.h>
#include "fetch_result.h"

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

// Execute an HTTPS GET request and stream the payload into a PSRAM buffer.
// Handles CA cert bundle configuration, timeouts, User-Agent, redirects and error mapping.
FetchResult http_get_psram(const char* url,
                           PsramBufferStream& out_buffer,
                           uint16_t connect_timeout_ms = 4000,
                           uint16_t total_timeout_ms = 8000);

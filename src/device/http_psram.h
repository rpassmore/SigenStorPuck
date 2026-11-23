#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Stream.h>

#include "fetch_result.h"
#include "psram_allocator.h"

// Bounded sink for callers that need a String body. HTTPClient::getString() has
// no cap at all; a broken or hostile host can otherwise exhaust internal RAM.
class LimitedStringStream : public Stream {
 public:
  explicit LimitedStringStream(size_t limit) : _limit(limit) {
    _string.reserve(limit < 4096 ? limit : 4096);
  }

  size_t write(uint8_t byte) override {
    if (_string.length() >= _limit) {
      _overflow = true;
      return 0;
    }
    _string += static_cast<char>(byte);
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (buffer == nullptr || size == 0) {
      return 0;
    }
    const size_t room = _string.length() < _limit ? _limit - _string.length() : 0;
    const size_t accepted = size < room ? size : room;
    if (accepted > 0) {
      _string.concat(reinterpret_cast<const char*>(buffer), accepted);
    }
    if (accepted < size) {
      _overflow = true;
    }
    return accepted;
  }

  const String& value() const { return _string; }
  size_t length() const { return _string.length(); }
  bool overflow() const { return _overflow; }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  String _string;
  size_t _limit;
  bool _overflow = false;
};

// Stream sink buffering HTTP responses in PSRAM without internal SRAM allocations.
class PsramBufferStream : public Stream {
 public:
  explicit PsramBufferStream(size_t capacity) {
    if (psramFound()) {
      _buffer = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_buffer) {
      psram_allocator_note_fallback(capacity);
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
      if (_buffer) {
        _overflow = true;
      }
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
      _overflow = true;
      return can_write;
    }
    memcpy(_buffer + _size, buffer, size);
    _size += size;
    _buffer[_size] = '\0';
    return size;
  }

  const char* c_str() const { return _buffer ? _buffer : ""; }
  size_t length() const { return _size; }
  size_t capacity() const { return _capacity; }
  bool isValid() const { return _buffer != nullptr; }
  bool overflow() const { return _overflow; }

  void clear() {
    _size = 0;
    _overflow = false;
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
  bool _overflow = false;
};

// Execute an HTTPS GET request and stream the payload into a PSRAM buffer.
// Handles CA cert bundle configuration, timeouts, User-Agent, redirects and error mapping.
FetchResult http_get_psram(const char* url,
                           PsramBufferStream& out_buffer,
                           uint16_t connect_timeout_ms = 4000,
                           uint16_t total_timeout_ms = 8000);

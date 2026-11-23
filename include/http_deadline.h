// Wall-clock deadline for a NetworkClient used by HTTPClient.
//
// HTTPClient::setTimeout() cannot be relied on for NetworkClientSecure: the
// socket option is applied to the wrong descriptor, so a server that completes
// TLS and then stalls mid-body can hold the poll task indefinitely. This arms
// an ESP-IDF timer that shuts the socket down at the deadline, which makes the
// in-flight read return instead of waiting forever.
//
// Two details make this safe:
//
//   * The callback does not call NetworkClient::stop(); it only shuts down the
//     lwIP socket. The owner (or HTTPClient) still stops the client normally.
//   * DeadlineClient wraps the client so any stop(), including one made inside
//     HTTPClient's error paths, cancels the timer first. cancel() waits for a
//     dispatched callback to finish before returning, so no shutdown can land
//     on a closed or reused descriptor.

#pragma once

#ifdef ARDUINO

#include <Arduino.h>
#include <NetworkClient.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>

#include <atomic>

class HttpDeadline {
 public:
  HttpDeadline(NetworkClient& client, uint32_t timeout_ms)
      : client_(client),
        timer_(nullptr),
        done_(xSemaphoreCreateBinary()),
        started_(false) {
    if (done_ == nullptr) {
      return;
    }

    esp_timer_create_args_t args = {};
    args.callback = [](void* arg) { static_cast<HttpDeadline*>(arg)->expire(); };
    args.arg = this;
    args.name = "http_deadline";
    if (esp_timer_create(&args, &timer_) != ESP_OK) {
      vSemaphoreDelete(done_);
      done_ = nullptr;
      timer_ = nullptr;
      return;
    }

    started_ = true;
    if (esp_timer_start_once(timer_, static_cast<uint64_t>(timeout_ms) * 1000) != ESP_OK) {
      started_ = false;
      cancel();
    }
  }

  ~HttpDeadline() {
    cancel();
    if (done_ != nullptr) {
      vSemaphoreDelete(done_);
      done_ = nullptr;
    }
  }

  HttpDeadline(const HttpDeadline&) = delete;
  HttpDeadline& operator=(const HttpDeadline&) = delete;

  bool expired() const { return expired_.load(); }

  // Cancels, drains any dispatched callback, then returns the final expiry
  // state. Callers should use the return value rather than sampling expired()
  // before the drain.
  bool cancel() {
    if (timer_ == nullptr) {
      return expired_.load();
    }
    // If the timer is still armed, removing it here means the callback can never
    // start. If it has already been dispatched, esp_timer_stop() fails and the
    // callback will post to done_ when it finishes; wait for that before the
    // client can be stopped and its fd closed.
    const esp_err_t stopped = esp_timer_stop(timer_);
    if (started_ && stopped != ESP_OK && done_ != nullptr) {
      xSemaphoreTake(done_, portMAX_DELAY);
    }
    esp_timer_delete(timer_);
    timer_ = nullptr;
    return expired_.load();
  }

 private:
  void expire() {
    expired_.store(true);
    const int fd = client_.fd();
    if (fd >= 0) {
      lwip_shutdown(fd, SHUT_RDWR);
    }
    // Must be last: cancel() uses this to prove the callback is finished.
    if (done_ != nullptr) {
      xSemaphoreGive(done_);
    }
  }

  NetworkClient& client_;
  esp_timer_handle_t timer_;
  SemaphoreHandle_t done_;
  bool started_;
  std::atomic<bool> expired_{false};
};

// Use in place of the concrete client passed to HTTPClient. Any stop(), even one
// made by HTTPClient itself on an error path, drains the deadline first.
template <typename Client>
class DeadlineClient : public Client {
 public:
  using Client::Client;

  void setDeadline(HttpDeadline* deadline) { deadline_ = deadline; }

  void stop() override {
    if (deadline_ != nullptr) {
      deadline_->cancel();
    }
    Client::stop();
  }

 private:
  HttpDeadline* deadline_ = nullptr;
};

#endif  // ARDUINO

/**
 * @file fault_breaker.h
 * @brief 连续故障熔断器（可复用）。
 *
 * 同类故障连续达到阈值后触发熔断；成功一次则清零计数。
 */
#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace visual {

class FaultBreaker {
 public:
  explicit FaultBreaker(int trip_threshold = 3) : trip_threshold_(trip_threshold < 1 ? 1 : trip_threshold) {}

  /** 记录一次失败。@return true 表示刚达到熔断阈值。 */
  bool OnFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++fail_count_;
    if (!tripped_ && fail_count_ >= trip_threshold_) {
      tripped_ = true;
      return true;
    }
    return false;
  }

  void OnSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    fail_count_ = 0;
    // 熔断后需外部 Reset 才恢复，避免抖动自动放开
  }

  bool IsTripped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tripped_;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    fail_count_ = 0;
    tripped_ = false;
  }

  int FailCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fail_count_;
  }

 private:
  int trip_threshold_;
  mutable std::mutex mutex_;
  int fail_count_ = 0;
  bool tripped_ = false;
};

}  // namespace visual

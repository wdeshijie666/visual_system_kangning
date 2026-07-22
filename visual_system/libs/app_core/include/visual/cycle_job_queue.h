/**
 * @file cycle_job_queue.h
 * @brief 周期任务队列：触发轮询与周期执行解耦（可复用）。
 *
 * 轮询线程只 Push；工作线程 TryPop 后执行。
 * 队列满时 Push 失败，由调用方决定丢弃或报警。
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace visual {

template <typename Job>
class CycleJobQueue {
 public:
  explicit CycleJobQueue(std::size_t max_size = 8) : max_size_(max_size == 0 ? 1 : max_size) {}

  /** @return false 表示队列已满，未入队。 */
  bool Push(Job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_) {
      return false;
    }
    queue_.push_back(std::move(job));
    cv_.notify_one();
    return true;
  }

  /** 阻塞等待直到有任务或 stop=true。 */
  std::optional<Job> PopWait(const std::atomic<bool>& running) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() { return !queue_.empty() || !running.load(); });
    if (queue_.empty()) {
      return std::nullopt;
    }
    Job job = std::move(queue_.front());
    queue_.pop_front();
    return job;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
  }

  void NotifyAll() { cv_.notify_all(); }

  std::size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  std::size_t max_size_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
};

}  // namespace visual

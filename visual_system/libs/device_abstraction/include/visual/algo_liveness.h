/**
 * @file algo_liveness.h
 * @brief 算法进程存活标志（无 Qt 依赖），供 SHM Run 轮询快速失败。
 */
#pragma once

#include <atomic>

namespace visual {

inline std::atomic<bool>& AlgoProcessAliveFlag() {
  static std::atomic<bool> alive{true};
  return alive;
}

inline void SetAlgoProcessAlive(bool alive) {
  AlgoProcessAliveFlag().store(alive, std::memory_order_release);
}

inline bool IsAlgoProcessAlive() {
  return AlgoProcessAliveFlag().load(std::memory_order_acquire);
}

}  // namespace visual

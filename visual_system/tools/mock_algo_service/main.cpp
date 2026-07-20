/**
 * @file main.cpp
 * @brief Mock 算法独立进程：在线 SHM 模式 / 离线历史数据回放模式。
 */
#include <filesystem>
#include <iostream>

#include "algo_config.h"
#include "algo_offline_replay.h"
#include "algo_online_service.h"
#include "visual/algo_run_mode.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

std::filesystem::path GetExeDir() {
#ifdef _WIN32
  char path[MAX_PATH]{};
  const DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(path).parent_path();
#else
  return std::filesystem::current_path();
#endif
}

}  // namespace

int main() {
  const auto exe_dir = GetExeDir();
  const algo::AlgoConfig config = algo::LoadAlgoConfig(exe_dir);

  if (config.mode == algo::RunMode::kOfflineReplay) {
    std::cout << "algo mode=" << visual::algo_config::kModeOfflineReplay << " (offline replay)\n";
    return algo::RunOfflineReplay(config);
  }

  std::cout << "algo mode=" << visual::algo_config::kModeOnline << " (online)\n";
  return algo::RunOnlineService(config);
}

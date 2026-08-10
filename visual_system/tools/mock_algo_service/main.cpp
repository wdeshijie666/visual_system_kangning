/**
 * @file main.cpp
 * @brief 算法独立进程入口。
 */
#include <filesystem>

#include "algo_config.h"
#include "algo_log.h"
#include "algo_offline_replay.h"
#include "algo_online_service.h"

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
#ifdef _WIN32
  // 管道输出按 UTF-8，供主程序 QString::fromUtf8 正确解析中文
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  const auto exe_dir = GetExeDir();
  const algo::AlgoConfig config = algo::LoadAlgoConfig(exe_dir);
  algo::SetAlgoLogLevel(config.log_level);

  algo::AlgoInfo(std::string("R05 参数文件：") + config.channel_r05.point_cloud.point_cloud_config);
  algo::AlgoInfo(std::string("R09 参数文件：") + config.channel_r09.point_cloud.point_cloud_config);

  if (config.mode == algo::RunMode::kOfflineReplay) {
    algo::AlgoInfo("当前为历史回放模式");
    return algo::RunOfflineReplay(config);
  }

  algo::AlgoInfo("当前为在线计算模式");
  return algo::RunOnlineService(config);
}

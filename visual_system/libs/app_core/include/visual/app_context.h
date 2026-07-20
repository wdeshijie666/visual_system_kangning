/**
 * @file app_context.h
 * @brief 全局配置：setting.json / devices.json。
 */
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "visual/i_plc_client.h"
#include "visual/capture_data_format.h"
#include "visual/run_mode.h"

namespace visual {

struct StationConfig {
  std::vector<std::string> camera_ids;
  std::vector<std::string> robots;
};

struct AppSettings {
  std::string app_name = "Visual System";
  std::string logo_path = "./resources/logo.png";
  std::string data_path = "./data";
  PlcConnectionOptions plc;
  StationConfig station_r05;
  StationConfig station_r09;
  std::string algo_shm_name = "Local\\VisualSystemAlgo_v2";
  int algo_timeout_ms = 30000;
  bool use_shm_algo = true;
  bool algo_transfer_depth = true;
  bool algo_transfer_pointcloud = false;
  /** 算法进程调试落盘：收到对应模态后写入 alg_program/data。 */
  bool algo_debug_save_depth = false;
  bool algo_debug_save_pointcloud = false;
  /** 视觉侧采集存根：默认只存深度，可再开点云。 */
  bool stub_save_depth = true;
  bool stub_save_pointcloud = false;
  /** 相对程序目录，默认 alg_program */
  std::string algo_program_dir = "alg_program";
  std::string algo_program_exe = "mock_algo_service.exe";
  RunMode run_mode = RunMode::kProduction;
  SimulationSettings simulation;
};

inline std::uint32_t BuildAlgoTransferFlags(const AppSettings& settings) {
  std::uint32_t flags = 0;
  if (settings.algo_transfer_depth) {
    flags |= static_cast<std::uint32_t>(AlgoTransferFlag::kDepth);
  }
  if (settings.algo_transfer_pointcloud) {
    flags |= static_cast<std::uint32_t>(AlgoTransferFlag::kPointCloud);
  }
  return flags;
}

inline bool IsSimulationMode(const AppSettings& settings) {
  return settings.run_mode == RunMode::kSimulation;
}

struct DeviceEntry {
  std::string id;
  std::string type = "rvc";
  std::string serial;
  std::string station;  // r05 / r09
};

class AppContext {
 public:
  static AppContext& Instance();

  bool Load();
  bool Save();

  AppSettings& Settings();
  const AppSettings& Settings() const;

  const std::map<std::string, DeviceEntry>& Devices() const;
  void SetDevices(const std::map<std::string, DeviceEntry>& devices);

  std::string ConfigPath() const;

 private:
  AppContext() = default;

  mutable std::mutex mutex_;
  AppSettings settings_;
  std::map<std::string, DeviceEntry> devices_;
};

}  // namespace visual

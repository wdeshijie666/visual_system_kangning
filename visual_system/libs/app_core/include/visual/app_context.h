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
  /** 工位总开关：false 时不接 PLC 触发、不参与启动联锁、算法通道关闭。手动触发仍可用。 */
  bool enabled = true;
  std::vector<std::string> camera_ids;
  std::vector<std::string> robots;
};

/** 单个算法 SHM 通道的映射名与互斥量名。 */
struct AlgoChannelSettings {
  std::string shm_name;
  std::string mutex_name;
};

struct AppSettings {
  std::string app_name = "Visual System";
  std::string logo_path = "./resources/logo.png";
  std::string data_path = "./data";
  /** 存根保留天数（DataStubRetentionCleaner）。 */
  int data_retention_days = 7;
  PlcConnectionOptions plc;
  StationConfig station_r05;
  StationConfig station_r09;
  /** 遗留单通道名；双工位模式下仅作兼容字段，实际用 algo_channel_r05/r09。 */
  std::string algo_shm_name = "Local\\VisualSystemAlgo_v2";
  AlgoChannelSettings algo_channel_r05;
  AlgoChannelSettings algo_channel_r09;
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

inline const StationConfig& StationSettings(const AppSettings& settings, StationId station) {
  return (station == StationId::kR09) ? settings.station_r09 : settings.station_r05;
}

inline bool IsStationEnabled(const AppSettings& settings, StationId station) {
  return StationSettings(settings, station).enabled;
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

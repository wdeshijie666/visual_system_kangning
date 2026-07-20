#pragma once

#include <filesystem>
#include <string>

#include "visual/algo_run_mode.h"

namespace algo {

enum class RunMode { kOnline, kOfflineReplay };

struct OfflineReplayOptions {
  std::filesystem::path data_dir;
  std::filesystem::path result_dir;
};

struct PipelineSimulationOptions {
  bool enabled = false;
  int image_width = 2040;
  int image_height = 1080;
  int status = 1;
  double offset_x_mm = 0.1;
  double offset_y_mm = 0.0;
  double offset_r_deg = 0.0;
  double diameter_mm = 100.0;
  double length_mm = 800.0;
};

struct AlgoConfig {
  RunMode mode = RunMode::kOnline;
  OfflineReplayOptions offline_replay;
  PipelineSimulationOptions pipeline_simulation;
  /** 收到 SHM 深度/点云后写入 exe 同目录 data/（调试用）。 */
  bool debug_save_depth = false;
  bool debug_save_pointcloud = false;
};

/** 从 exe 同目录加载 algo_config.json；不存在时使用在线模式默认值。 */
AlgoConfig LoadAlgoConfig(const std::filesystem::path& exe_dir);

/** 将 mode 写回在线模式（0）。 */
bool SaveAlgoOnlineMode(const std::filesystem::path& exe_dir);

std::filesystem::path ResolveConfigPath(const std::filesystem::path& exe_dir, const std::string& configured);

}  // namespace algo

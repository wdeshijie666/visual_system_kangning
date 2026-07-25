#pragma once

#include <filesystem>
#include <string>

#include "algo_log.h"
#include "visual/algo_run_mode.h"

namespace algo {

enum class RunMode { kOnline, kOfflineReplay };

struct OfflineReplayOptions {
  std::filesystem::path data_dir;
  std::filesystem::path result_dir;
};

struct PipelineSimulationOptions {
  bool enabled = false;
  int image_width = 2448;
  int image_height = 2048;
  int status = 1;
  double offset_x_mm = 0.1;
  double offset_y_mm = 0.0;
  double offset_r_deg = 0.0;
  double diameter_mm = 100.0;
  double length_mm = 800.0;
};

/** 算法进程侧单通道配置（与 setting.json algo.channels 对齐）。 */
struct AlgoChannelConfig {
  bool enabled = true;
  std::string shm_name;
  std::string mutex_name;
};

struct AlgoConfig {
  RunMode mode = RunMode::kOnline;
  OfflineReplayOptions offline_replay;
  PipelineSimulationOptions pipeline_simulation;
  /** 收到 SHM 深度/点云后写入 exe 同目录 data/（调试用）。 */
  bool debug_save_depth = false;
  bool debug_save_pointcloud = false;
  /** 双工位通道名；空则使用 algo_shm_layout 默认常量。 */
  AlgoChannelConfig channel_r05;
  AlgoChannelConfig channel_r09;
  /**
   * 是否调用真实 PointCloudProcessor（false 时走 Mock/仿真填 Log）。
   * 未编译 VS_HAS_POINTCLOUD_ALGO 时强制为 false。
   */
  bool use_point_cloud_algo = true;
  /** PointCloudProcessor 配置文件，相对算法 exe 目录或绝对路径。 */
  std::string point_cloud_config = "config.json";
  /** process(depth, top_n) 的 top_n，通常为 5（与 PLC Log 条数一致）。 */
  int point_cloud_top_n = 5;
  /**
   * 临时：非空时用该 TIFF（毫米）代替 SHM 深度。
   */
  std::string temp_force_depth_tiff;
  /** 与 setting.json algo.transferDepth / transferPointcloud 对齐，决定 SHM arena 预留。 */
  bool transfer_depth = true;
  bool transfer_pointcloud = false;
  bool transfer_gray = true;
  /** 日志等级：info=产线日常，debug=详细排查。 */
  LogLevel log_level = LogLevel::kInfo;
};

/** 从 exe 同目录加载 algo_config.json；不存在时使用在线模式默认值。 */
AlgoConfig LoadAlgoConfig(const std::filesystem::path& exe_dir);

/** 将 mode 写回在线模式（0）。 */
bool SaveAlgoOnlineMode(const std::filesystem::path& exe_dir);

std::filesystem::path ResolveConfigPath(const std::filesystem::path& exe_dir, const std::string& configured);

}  // namespace algo

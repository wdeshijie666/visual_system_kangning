/**
 * @file algo_config.h
 * @brief 算法进程 algo_config.json 解析：双通道 SHM 与按工位点云引擎参数。
 */
#pragma once

#include <filesystem>
#include <string>

#include "algo_log.h"
#include "visual/algo_run_mode.h"
#include "visual/algo_shm_layout.h"

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

/** 单工位 PointCloudProcessor 相关参数（可与另一工位不同）。 */
struct PointCloudAlgoOptions {
  bool use_point_cloud_algo = true;
  /** 相对算法 exe 目录或绝对路径，如 config.json / config_r05.json。 */
  std::string point_cloud_config = "config.json";
  /** process(depth, top_n)，通常为 5（与 PLC Log 条数一致）。 */
  int point_cloud_top_n = 5;
};

/** 算法进程侧单通道配置（与 setting.json algo.channels 对齐）。 */
struct AlgoChannelConfig {
  bool enabled = true;
  std::string shm_name;
  std::string mutex_name;
  PointCloudAlgoOptions point_cloud;
};

struct AlgoConfig {
  RunMode mode = RunMode::kOnline;
  OfflineReplayOptions offline_replay;
  PipelineSimulationOptions pipeline_simulation;
  /** 收到 SHM 深度/点云后写入 exe 同目录 data/（调试用）。 */
  bool debug_save_depth = false;
  bool debug_save_pointcloud = false;
  /** 双工位通道；点云参数以 channel_*.point_cloud 为准。 */
  AlgoChannelConfig channel_r05;
  AlgoChannelConfig channel_r09;
  /**
   * 顶层默认（兼容旧 JSON）：加载时若通道未写点云字段则用此值填充。
   * 新配置请写在 channels.r05 / channels.r09 下。
   */
  PointCloudAlgoOptions point_cloud_defaults;
  /**
   * 两工位共用参考点 JSON（相对算法 exe 目录或绝对路径）。
   * 缺文件时仍构造引擎，由算法库使用默认参考点。
   */
  std::string reference_point_config = "reference_point.json";
  /**
   * 临时：非空时用该 TIFF（毫米）代替 SHM 深度（两工位共用调试开关）。
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

/** 取指定 SHM 通道的点云算法选项。 */
const PointCloudAlgoOptions& PointCloudOptionsForChannel(const AlgoConfig& config,
                                                        visual::shm::ShmChannelId channel);

/** 按 header.station_id（5/7/9）解析工位点云选项。 */
const PointCloudAlgoOptions& PointCloudOptionsForStationId(const AlgoConfig& config,
                                                          std::int32_t station_id);

/** 任一工位启用真实点云算法则为 true（用于通路仿真开关等全局判断）。 */
bool AnyChannelUsesPointCloudAlgo(const AlgoConfig& config);

}  // namespace algo

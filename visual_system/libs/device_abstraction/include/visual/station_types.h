/**

 * @file station_types.h

 * @brief 工位、采集与算法交互通用类型（与 AB PLC VisionLogResult 对齐）。

 *

 * 周期数据流中的关键载体：

 *   CaptureBundle  — 单相机采集结果（内存 buffer + 预生成落盘路径）

 *   AlgoRequest    — SequenceEngine → IAlgoService / SHM

 *   AlgoResponse   — 算法 → SequenceEngine（5 条 LogResult）

 *

 * 详见 docs/框架流程通路.md §6.2、§6.3

 */

#pragma once



#include <array>

#include <cstdint>

#include <memory>

#include <string>

#include <vector>



#include "visual/capture_data_format.h"



namespace visual {



enum class StationId : std::uint8_t { kR05 = 5, kR07 = 7, kR09 = 9 };



enum class InspectStatus : std::int32_t {

  kDefault = 0,

  kOk = 1,

  kNg = 2,

  kNotLog = 3,

};



/** 单工件检测结果，与 PLC CameraToPLC_Int 结果区字段对应。 */

struct LogResult {

  InspectStatus status = InspectStatus::kDefault;

  double offset_x_mm = 0.0;

  double offset_y_mm = 0.0;

  double offset_r_deg = 0.0;

  double diameter_mm = 0.0;

  double length_mm = 0.0;

};



constexpr std::size_t kLogCountPerStation = 5;

using LogResultBatch = std::array<LogResult, kLogCountPerStation>;



/** 内存中的深度图，在线模式下经 SHM blob 传给算法。 */

struct DepthImageBuffer {

  std::uint32_t width = 0;

  std::uint32_t height = 0;

  DepthPixelFormat format = DepthPixelFormat::kNone;

  std::vector<std::uint8_t> data;

};



/** 内存中的点云，在线模式下经 SHM blob 传给算法。 */

struct PointCloudBuffer {

  PointCloudFormat format = PointCloudFormat::kNone;

  std::uint64_t point_count = 0;

  std::vector<std::uint8_t> data;

};



/** 内存中的灰度图（Mono8），仅供 UI 预览，不落盘。 */

struct GrayImageBuffer {

  std::uint32_t width = 0;

  std::uint32_t height = 0;

  std::vector<std::uint8_t> data;

};



/**

 * 单相机一次采集的完整载荷。

 * - 在线：depth/pointcloud/gray 填内存；落盘由 dataStub.saveDepth/savePointcloud 控制

 * - 回放：depth_path 等路径有效

 */

struct CaptureBundle {

  std::string rgb_path;       /**< 已废弃，不再落盘 */

  std::string depth_path;     /**< dataStub.saveDepth 时写入 */

  std::string pointcloud_path; /**< dataStub.savePointcloud 时写入 */

  std::string camera_serial;

  bool ok = false;

  /** Capture 失败时的简短原因（供日志）。 */
  std::string error_message;

  std::shared_ptr<DepthImageBuffer> depth;

  std::shared_ptr<PointCloudBuffer> pointcloud;

  std::shared_ptr<GrayImageBuffer> gray;

};



/** 视觉 → 算法的单次计算请求。 */

struct AlgoRequest {

  StationId station = StationId::kR05;

  AlgoInputMode input_mode = AlgoInputMode::kOnlineShm;

  std::uint32_t transfer_flags = 0;  /**< setting.json transferDepth / transferPointcloud */

  std::string session_dir;           /**< 归档目录；回放模式下算法从此读盘 */

  std::vector<CaptureBundle> captures;

};



/** 算法 → 视觉的单次计算响应。 */

struct AlgoResponse {

  bool ok = false;

  std::string message;

  LogResultBatch logs{};

};



}  // namespace visual



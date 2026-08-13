/**
 * @file algo_pointcloud_runner.h
 * @brief 真实点云算法封装：SHM 深度 → PointCloudProcessor → 5 条 Log。
 */
#pragma once

#include <filesystem>
#include <string>

#include "algo_config.h"
#include "algo_processor_create.h"
#include "visual/algo_shm_layout.h"

namespace algo {

/**
 * 每通道引擎槽（R05/R09 不共用实例）。
 * 有有效深度时每周期先析构再构造，使构造函数重新加载点云/参考点配置。
 */
struct PointCloudProcessorSlot {
  PointCloudProcessorPtr processor;
};

/**
 * 从 SHM 读取第一路有效深度并跑圆柱拟合；开启 transferGray 时写回可视化图。
 * 若 input_mode=kOfflinePath，则从 session_dir 读 `_depth.*`（毫米），不读 SHM blob。
 * @param channel_pc 非空时优先用该工位点云配置（在线双线程应传入本通道配置）；
 *                   空则按 header.station_id 回退解析。
 * @param slot 非空时写入该通道槽（每周期重建）；空则本请求临时构造。
 * @return false 表示输入无效或算法失败；无有效圆柱仍返回 true（logs 为 NG）。
 */
bool RunPointCloudFromShm(visual::shm::ShmHeader* header, std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, const AlgoConfig& config,
                          const std::filesystem::path& exe_dir, visual::shm::ShmLogResult* logs,
                          std::size_t log_count, std::string* error,
                          PointCloudProcessorSlot* slot = nullptr,
                          const PointCloudAlgoOptions* channel_pc = nullptr);

}  // namespace algo

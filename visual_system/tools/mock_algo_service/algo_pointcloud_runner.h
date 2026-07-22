/**
 * @file algo_pointcloud_runner.h
 * @brief 真实点云算法封装：SHM 深度 → PointCloudProcessor → 5 条 Log。
 */
#pragma once

#include <filesystem>
#include <string>

#include "algo_config.h"
#include "visual/algo_shm_layout.h"

class PointCloudProcessor;

namespace algo {

/**
 * 从 SHM 读取第一路有效深度图并跑圆柱拟合。
 * @param processor 非空时复用该实例（每通道线程各持有一个）；空则临时构造。
 * @return false 表示输入无效或算法抛错级失败（写入 error）。
 *         无有效圆柱时仍返回 true，logs 填 NG。
 */
bool RunPointCloudFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, const AlgoConfig& config,
                          const std::filesystem::path& exe_dir, visual::shm::ShmLogResult* logs,
                          std::size_t log_count, std::string* error,
                          ::PointCloudProcessor* processor = nullptr);

}  // namespace algo

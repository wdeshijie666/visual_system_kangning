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

/** 每通道独立引擎；分辨率变化时重建，避免 DLL 内部按旧尺寸缓存崩溃。 */
struct PointCloudProcessorSlot {
  PointCloudProcessorPtr processor;
  int last_depth_w = -1;
  int last_depth_h = -1;
};

/**
 * 从 SHM 读取第一路有效深度图并跑圆柱拟合。
 * @param slot 非空时复用/按分辨率重建该通道实例；空则临时构造。
 * @return false 表示输入无效或算法抛错级失败（写入 error）。
 *         无有效圆柱时仍返回 true，logs 填 NG。
 */
bool RunPointCloudFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, const AlgoConfig& config,
                          const std::filesystem::path& exe_dir, visual::shm::ShmLogResult* logs,
                          std::size_t log_count, std::string* error,
                          PointCloudProcessorSlot* slot = nullptr);

}  // namespace algo

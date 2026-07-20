/**
 * @file algo_shm_codec.h
 * @brief SHM v2 请求载荷编解码（视觉侧写入 / 算法侧读取）。
 */
#pragma once

#include "visual/algo_shm_layout.h"
#include "visual/station_types.h"

namespace visual::shm {

/** 将 AlgoRequest 写入 SHM 头与 blob 区（在线模式拷贝像素；离线模式仅写路径）。 */
bool WriteRequestToShm(ShmHeader* header, std::uint8_t* blob_arena, std::size_t blob_arena_size,
                       const AlgoRequest& req, std::string* error);

/** 算法侧：从 SHM 解析输入（预留转换入口，当前仅校验元数据）。 */
bool ValidateShmRequest(const ShmHeader* header, const std::uint8_t* blob_arena, std::size_t blob_arena_size,
                        std::string* error);

}  // namespace visual::shm

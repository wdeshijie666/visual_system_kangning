#pragma once

#include <filesystem>
#include <string>

#include "algo_config.h"
#include "visual/algo_shm_layout.h"

namespace algo {

/**
 * 将 SHM 中的深度/点云载荷转换为算法内部输入（接口未定，当前为预留桩）。
 * @param config 含 debugSaveDepth / debugSavePointcloud 时写入 exe_dir/data。
 * @param exe_dir 算法程序目录（用于 data/ 落盘）。
 */
bool PrepareAlgoInputFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                             std::size_t blob_arena_size, const AlgoConfig& config,
                             const std::filesystem::path& exe_dir, std::string* error);

/** 离线模式：从 session_dir 加载数据（接口未定，当前为预留桩）。 */
bool PrepareAlgoInputFromPaths(const visual::shm::ShmHeader* header, std::string* error);

}  // namespace algo

/**
 * @file algo_input_converter.h
 * @brief 算法侧输入准备：在线 SHM 校验/调试落盘；离线按 session 目录或指定深度文件读图。
 */
#pragma once

#include <filesystem>
#include <string>

#include "algo_config.h"
#include "visual/algo_shm_layout.h"

namespace algo {

/**
 * 对 SHM 中的深度/点云载荷做校验，并按配置可选落盘调试文件。
 * @param config 开 debugSaveDepth / debugSavePointcloud 时写入 exe_dir/data。
 * @param exe_dir 算法程序目录（用于 data/ 落盘）。
 */
bool PrepareAlgoInputFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                             std::size_t blob_arena_size, const AlgoConfig& config,
                             const std::filesystem::path& exe_dir, std::string* error);

/**
 * 离线回放：校验 session_dir（可为会话目录，或强制指定的深度文件路径）。
 * 不加载像素；实际读盘在 RunPointCloudFromShm 的 kOfflinePath 分支。
 */
bool PrepareAlgoInputFromPaths(const visual::shm::ShmHeader* header, std::string* error);

/**
 * 解析离线深度路径：session_dir 若已是深度文件则直接用；否则在目录内扫描。
 */
bool ResolveOfflineDepthPath(const visual::shm::ShmHeader* header, std::filesystem::path* out_path,
                             std::string* error);

/** station_id（5/9）→ 落盘文件名中的工位标签 R05/R09；未知则空串。 */
std::string StationTagFromShmId(std::int32_t station_id);

/**
 * 在会话目录中查找深度图（仅 `_depth.`，不含 ply）。
 * 优先匹配 station_tag + camera_id；否则回退到同工位任意深度文件。
 * @return 找到返回 true，并写入 out_path。
 */
bool FindDepthFileInSession(const std::filesystem::path& session_dir,
                            const std::string& station_tag, const std::string& camera_id,
                            std::filesystem::path* out_path, std::string* error);

/**
 * 查找同命名规则的灰度图 `_gray.pgm`（可选；找不到不报错，由调用方决定）。
 */
bool FindGrayFileInSession(const std::filesystem::path& session_dir,
                           const std::string& station_tag, const std::string& camera_id,
                           std::filesystem::path* out_path);

}  // namespace algo

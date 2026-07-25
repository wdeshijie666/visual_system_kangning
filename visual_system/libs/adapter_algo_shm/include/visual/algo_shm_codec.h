/**
 * @file algo_shm_codec.h
 * @brief SHM 请求/结果图像编解码（视觉侧写入请求，算法侧写回可视化图）。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "visual/algo_shm_layout.h"
#include "visual/station_types.h"

namespace visual::shm {

/** 将 AlgoRequest 写入 SHM 头与 blob 区（在线模式拷贝像素；离线模式仅写路径）。 */
bool WriteRequestToShm(ShmHeader* header, std::uint8_t* blob_arena, std::size_t blob_arena_size,
                       const AlgoRequest& req, std::string* error);

/** 算法侧：校验 SHM 请求元数据。 */
bool ValidateShmRequest(const ShmHeader* header, const std::uint8_t* blob_arena,
                        std::size_t blob_arena_size, std::string* error);

/**
 * 算法完成后写回可视化图像到指定相机的 image 槽（需已开启 transferGray）。
 * 覆盖请求时写入的 Mono8；容量上限为 kMaxImageBytes。
 */
bool WriteResultImageToShm(ShmHeader* header, std::uint8_t* blob_arena, std::size_t blob_arena_size,
                           int camera_index, std::uint32_t width, std::uint32_t height,
                           ImagePixelFormat format, const std::uint8_t* pixels, std::size_t byte_count,
                           std::string* error);

/** 视觉侧在 kDone 后读取第一路有效结果图像（无则返回 false）。 */
bool ReadFirstResultImage(const ShmHeader* header, const std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, std::vector<std::uint8_t>* out_pixels,
                          std::uint32_t* out_width, std::uint32_t* out_height,
                          ImagePixelFormat* out_format, std::string* error);

}  // namespace visual::shm

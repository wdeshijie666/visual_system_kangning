/**
 * @file algo_shm_layout.h
 * @brief 视觉主进程与算法进程共享内存布局（Win32 命名映射，v2）。
 *
 * 内存布局：[ShmHeader][blob arena]
 *   - Header：状态机、工位、input_mode、transfer_flags、5 条 Log 输出、每相机元数据
 *   - Arena：每相机槽位 kCameraBlobBytes（深度 + 点云最大字节）
 *
 * 状态机：kIdle → kRequestPosted → kBusy → kDone | kError → kIdle
 * 详见 docs/框架流程通路.md §6.3、docs/algo_shm_protocol.md
 */
#pragma once

#include <array>
#include <cstdint>

#include "visual/capture_data_format.h"

namespace visual::shm {

inline constexpr char kShmName[] = "Local\\VisualSystemAlgo_v2";
inline constexpr char kMutexName[] = "Local\\VisualSystemAlgoMutex_v2";
inline constexpr std::uint32_t kMagic = 0x56414C47;  // VALG
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::size_t kMaxCameras = 4;
inline constexpr std::size_t kLogCount = 5;

/** 单相机最大深度/点云字节数（覆盖常见 RVC X2 分辨率，如 2448×2048）。 */
inline constexpr std::size_t kMaxImageWidth = 2560;
inline constexpr std::size_t kMaxImageHeight = 2200;
inline constexpr std::size_t kMaxDepthBytes = kMaxImageWidth * kMaxImageHeight * sizeof(double);
inline constexpr std::size_t kMaxPointCloudBytes = kMaxImageWidth * kMaxImageHeight * 3 * sizeof(double);
inline constexpr std::size_t kCameraBlobBytes = kMaxDepthBytes + kMaxPointCloudBytes;
inline constexpr std::size_t kBlobArenaSize = kMaxCameras * kCameraBlobBytes;

enum class State : std::uint32_t {
  kIdle = 0,           /**< 空闲，可接受新请求 */
  kRequestPosted = 1,  /**< 视觉侧已写入请求，等待算法取走 */
  kBusy = 2,           /**< 算法正在计算 */
  kDone = 3,           /**< 算法完成，logs[] 有效 */
  kError = 4,          /**< 算法失败，error_message 有效 */
};

#pragma pack(push, 1)

struct ShmLogResult {
  std::int32_t status = 0;
  double offset_x_mm = 0.0;
  double offset_y_mm = 0.0;
  double offset_r_deg = 0.0;
  double diameter_mm = 0.0;
  double length_mm = 0.0;
};

/** 深度图元数据：描述 blob 区内一段数据的解析方式。 */
struct ShmDepthMeta {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  DepthPixelFormat format = DepthPixelFormat::kNone;
  std::uint32_t bytes_per_pixel = 0;
  std::uint32_t row_stride_bytes = 0;
  std::uint64_t blob_offset = 0;
  std::uint64_t blob_size = 0;
};

/** 点云元数据。 */
struct ShmPointCloudMeta {
  PointCloudFormat format = PointCloudFormat::kNone;
  std::uint64_t point_count = 0;
  std::uint64_t blob_offset = 0;
  std::uint64_t blob_size = 0;
};

/** 单相机载荷描述（在线模式 blob 在 arena 中按 offset 定位）。 */
struct ShmCameraPayload {
  char camera_serial[64]{};
  ShmDepthMeta depth{};
  ShmPointCloudMeta pointcloud{};
};

struct ShmHeader {
  std::uint32_t magic = kMagic;
  std::uint32_t version = kVersion;
  std::uint32_t seq_id = 0;
  State state = State::kIdle;
  std::int32_t station_id = 5;
  std::int32_t camera_count = 0;
  /** @see AlgoInputMode */
  std::uint32_t input_mode = 0;
  /** @see AlgoTransferFlag，在线模式有效 */
  std::uint32_t transfer_flags = 0;
  char session_dir[512]{};
  char error_message[256]{};
  ShmLogResult logs[kLogCount]{};
  ShmCameraPayload cameras[kMaxCameras]{};
};

#pragma pack(pop)

inline constexpr std::size_t kShmHeaderSize = sizeof(ShmHeader);
inline constexpr std::size_t kShmTotalSize = kShmHeaderSize + kBlobArenaSize;

inline const std::uint8_t* BlobArenaBase(const ShmHeader* header) {
  return reinterpret_cast<const std::uint8_t*>(header) + kShmHeaderSize;
}

inline std::uint8_t* BlobArenaBase(ShmHeader* header) {
  return reinterpret_cast<std::uint8_t*>(header) + kShmHeaderSize;
}

}  // namespace visual::shm

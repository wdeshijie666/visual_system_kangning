/**
 * @file algo_shm_layout.h
 * @brief 视觉主进程与算法进程共享内存布局（Win32 命名映射）。
 *
 * 双工位并行（v5）：R05 / R09 各一块映射 + 独立互斥量 + 投递事件。
 * 使用 Global\ 前缀，避免 Local\ 在不同完整性级别/会话下互不可见。
 * 布局：[ShmHeader][blob arena]
 */
#pragma once

#include <cstdint>

#include "visual/capture_data_format.h"
#include "visual/station_types.h"

namespace visual::shm {

/** 遗留单通道名（仅兼容文档/旧配置；运行时默认走双通道）。 */
inline constexpr char kShmName[] = "Global\\VisualSystemAlgo_v2";
inline constexpr char kMutexName[] = "Global\\VisualSystemAlgoMutex_v2";

/** 双工位逻辑通道：R07 并入 R05 通道。 */
enum class ShmChannelId : std::uint8_t { kR05 = 5, kR09 = 9 };

/** v5：Global 命名空间，彻底避开 Local\\ 会话隔离。 */
inline constexpr char kShmNameR05[] = "Global\\VisualSystemAlgo_R05_v5";
inline constexpr char kMutexNameR05[] = "Global\\VisualSystemAlgoMutex_R05_v5";
inline constexpr char kEventNameR05[] = "Global\\VisualSystemAlgoEvent_R05_v5";
inline constexpr char kShmNameR09[] = "Global\\VisualSystemAlgo_R09_v5";
inline constexpr char kMutexNameR09[] = "Global\\VisualSystemAlgoMutex_R09_v5";
inline constexpr char kEventNameR09[] = "Global\\VisualSystemAlgoEvent_R09_v5";

inline const char* ShmNameForChannel(ShmChannelId channel) {
  return channel == ShmChannelId::kR09 ? kShmNameR09 : kShmNameR05;
}

inline const char* MutexNameForChannel(ShmChannelId channel) {
  return channel == ShmChannelId::kR09 ? kMutexNameR09 : kMutexNameR05;
}

inline const char* EventNameForChannel(ShmChannelId channel) {
  return channel == ShmChannelId::kR09 ? kEventNameR09 : kEventNameR05;
}

/** R05/R07 → R05 通道；R09 → R09 通道。 */
inline ShmChannelId ToShmChannel(StationId station) {
  return station == StationId::kR09 ? ShmChannelId::kR09 : ShmChannelId::kR05;
}

inline constexpr std::uint32_t kMagic = 0x56414C47;  // VALG
/** Header：kMaxCameras=2，blob_arena_bytes，version=5。 */
inline constexpr std::uint32_t kVersion = 5;
inline constexpr std::size_t kMaxCameras = 2;
inline constexpr std::size_t kLogCount = 5;

/** 单相机最大深度/点云字节数（覆盖常见 RVC X2 分辨率，如 2448×2048）。 */
inline constexpr std::size_t kMaxImageWidth = 2560;
inline constexpr std::size_t kMaxImageHeight = 2200;
inline constexpr std::size_t kMaxDepthBytes = kMaxImageWidth * kMaxImageHeight * sizeof(double);
inline constexpr std::size_t kMaxPointCloudBytes =
    kMaxImageWidth * kMaxImageHeight * 3 * sizeof(double);

inline bool TransferHasDepth(std::uint32_t transfer_flags) {
  return HasTransferFlag(transfer_flags, AlgoTransferFlag::kDepth);
}

inline bool TransferHasPointCloud(std::uint32_t transfer_flags) {
  return HasTransferFlag(transfer_flags, AlgoTransferFlag::kPointCloud);
}

/** 单相机槽位字节：按 transfer 开关决定是否预留深度/点云。 */
inline std::size_t CameraBlobStrideBytes(std::uint32_t transfer_flags) {
  std::size_t n = 0;
  if (TransferHasDepth(transfer_flags)) {
    n += kMaxDepthBytes;
  }
  if (TransferHasPointCloud(transfer_flags)) {
    n += kMaxPointCloudBytes;
  }
  return n;
}

inline std::size_t BlobArenaSizeForFlags(std::uint32_t transfer_flags) {
  return kMaxCameras * CameraBlobStrideBytes(transfer_flags);
}

/** 两模态都开时的上限（建映射/兼容用）。 */
inline constexpr std::size_t kMaxCameraBlobBytes = kMaxDepthBytes + kMaxPointCloudBytes;
inline constexpr std::size_t kMaxBlobArenaSize = kMaxCameras * kMaxCameraBlobBytes;

enum class State : std::uint32_t {
  kIdle = 0,
  kRequestPosted = 1,
  kBusy = 2,
  kDone = 3,
  kError = 4,
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

struct ShmDepthMeta {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  DepthPixelFormat format = DepthPixelFormat::kNone;
  std::uint32_t bytes_per_pixel = 0;
  std::uint32_t row_stride_bytes = 0;
  std::uint64_t blob_offset = 0;
  std::uint64_t blob_size = 0;
};

struct ShmPointCloudMeta {
  PointCloudFormat format = PointCloudFormat::kNone;
  std::uint64_t point_count = 0;
  std::uint64_t blob_offset = 0;
  std::uint64_t blob_size = 0;
};

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
  std::uint32_t input_mode = 0;
  std::uint32_t transfer_flags = 0;
  /** 实际 blob arena 字节数（视觉建映射时写入，算法优先采信）。 */
  std::uint64_t blob_arena_bytes = 0;
  /** 视觉进程 PID（建映射时写入，便于对照日志）。 */
  std::uint32_t vision_pid = 0;
  std::uint32_t reserved0 = 0;
  char session_dir[512]{};
  char error_message[256]{};
  ShmLogResult logs[kLogCount]{};
  ShmCameraPayload cameras[kMaxCameras]{};
};

#pragma pack(pop)

inline constexpr std::size_t kShmHeaderSize = sizeof(ShmHeader);

inline std::size_t ShmTotalSizeForFlags(std::uint32_t transfer_flags) {
  return kShmHeaderSize + BlobArenaSizeForFlags(transfer_flags);
}

inline constexpr std::size_t kMaxShmTotalSize = kShmHeaderSize + kMaxBlobArenaSize;
inline constexpr std::size_t kShmTotalSize = kMaxShmTotalSize;
inline constexpr std::size_t kBlobArenaSize = kMaxBlobArenaSize;
inline constexpr std::size_t kCameraBlobBytes = kMaxCameraBlobBytes;

inline const std::uint8_t* BlobArenaBase(const ShmHeader* header) {
  return reinterpret_cast<const std::uint8_t*>(header) + kShmHeaderSize;
}

inline std::uint8_t* BlobArenaBase(ShmHeader* header) {
  return reinterpret_cast<std::uint8_t*>(header) + kShmHeaderSize;
}

}  // namespace visual::shm

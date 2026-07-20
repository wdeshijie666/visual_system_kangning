/**

 * @file algo_shm_codec.cpp

 * @brief SHM v2 载荷编解码：视觉侧 WriteRequestToShm / 算法侧 ValidateShmRequest。

 *

 * 在线（kOnlineShm）：

 *   按 transfer_flags 将 CaptureBundle 中 depth/pointcloud 拷贝到 blob arena，

 *   并在 ShmCameraPayload 中填写 ShmDepthMeta / ShmPointCloudMeta 供算法解析。

 * 回放（kOfflinePath）：

 *   仅写 session_dir 与 camera_serial，算法从磁盘读取（见 algo_input_converter.cpp）。

 *

 * 详见 docs/框架流程通路.md §6.3、§8

 */

#include "visual/algo_shm_codec.h"



#include <algorithm>

#include <cstring>



namespace visual::shm {

namespace {



std::uint32_t BytesPerDepthPixel(DepthPixelFormat fmt) {

  switch (fmt) {

    case DepthPixelFormat::kUint16Mm:

      return 2;

    case DepthPixelFormat::kFloat32Mm:

      return 4;

    case DepthPixelFormat::kFloat64Mm:

      return 8;

    default:

      return 0;

  }

}



bool CopyBlob(std::uint8_t* arena, std::size_t arena_size, std::uint64_t offset, const std::uint8_t* src,

              std::size_t size, std::string* error) {

  if (size == 0) {

    return true;

  }

  if (src == nullptr) {

    if (error != nullptr) {

      *error = "null blob source";

    }

    return false;

  }

  if (offset + size > arena_size) {

    if (error != nullptr) {

      *error = "shm blob arena overflow";

    }

    return false;

  }

  std::memcpy(arena + offset, src, size);

  return true;

}



/** 每相机固定槽位，避免不同请求间 blob 重叠。 */

std::uint64_t CameraBlobBaseOffset(std::size_t camera_index) {

  return static_cast<std::uint64_t>(camera_index) * kCameraBlobBytes;

}



}  // namespace



bool WriteRequestToShm(ShmHeader* header, std::uint8_t* blob_arena, std::size_t blob_arena_size,

                       const AlgoRequest& req, std::string* error) {

  if (header == nullptr) {

    if (error != nullptr) {

      *error = "null shm header";

    }

    return false;

  }



  const auto camera_count = static_cast<std::int32_t>(

      std::min(req.captures.size(), static_cast<std::size_t>(kMaxCameras)));

  header->camera_count = camera_count;

  header->input_mode = static_cast<std::uint32_t>(req.input_mode);

  header->transfer_flags = req.transfer_flags;

  std::memset(header->cameras, 0, sizeof(header->cameras));



  if (blob_arena != nullptr && blob_arena_size > 0) {

    std::memset(blob_arena, 0, blob_arena_size);

  }



  // ----- 回放分支：历史数据回放，算法从 session_dir 读盘 -----

  if (req.input_mode == AlgoInputMode::kOfflinePath) {

    std::strncpy(header->session_dir, req.session_dir.c_str(), sizeof(header->session_dir) - 1);

    for (std::int32_t i = 0; i < camera_count; ++i) {

      const auto& cap = req.captures[static_cast<std::size_t>(i)];

      std::strncpy(header->cameras[i].camera_serial, cap.camera_serial.c_str(),

                   sizeof(header->cameras[i].camera_serial) - 1);

    }

    return true;

  }



  // ----- 在线分支：SHM 直传像素，不写 session_dir 给算法 -----

  header->session_dir[0] = '\0';



  for (std::int32_t i = 0; i < camera_count; ++i) {

    const auto& cap = req.captures[static_cast<std::size_t>(i)];

    auto& payload = header->cameras[i];

    std::strncpy(payload.camera_serial, cap.camera_serial.c_str(), sizeof(payload.camera_serial) - 1);



    const std::uint64_t base = CameraBlobBaseOffset(static_cast<std::size_t>(i));

    std::uint64_t cursor = base;



    if (HasTransferFlag(req.transfer_flags, AlgoTransferFlag::kDepth) && cap.depth &&

        !cap.depth->data.empty()) {

      const auto& depth = *cap.depth;

      const std::uint32_t bpp = BytesPerDepthPixel(depth.format);

      if (bpp == 0) {

        if (error != nullptr) {

          *error = "unsupported depth format";

        }

        return false;

      }

      const std::size_t expected =

          static_cast<std::size_t>(depth.width) * depth.height * bpp;

      if (depth.data.size() < expected) {

        if (error != nullptr) {

          *error = "depth buffer too small";

        }

        return false;

      }

      if (expected > kMaxDepthBytes) {

        if (error != nullptr) {

          *error = "depth exceeds shm limit";

        }

        return false;

      }



      payload.depth.width = depth.width;

      payload.depth.height = depth.height;

      payload.depth.format = depth.format;

      payload.depth.bytes_per_pixel = bpp;

      payload.depth.row_stride_bytes = depth.width * bpp;

      payload.depth.blob_offset = cursor;

      payload.depth.blob_size = expected;



      if (!CopyBlob(blob_arena, blob_arena_size, cursor, depth.data.data(), expected, error)) {

        return false;

      }

      cursor += expected;

    }



    if (HasTransferFlag(req.transfer_flags, AlgoTransferFlag::kPointCloud) && cap.pointcloud &&

        !cap.pointcloud->data.empty()) {

      const auto& pc = *cap.pointcloud;

      const std::size_t size = pc.data.size();

      if (size > kMaxPointCloudBytes) {

        if (error != nullptr) {

          *error = "pointcloud exceeds shm limit";

        }

        return false;

      }



      payload.pointcloud.format = pc.format;

      payload.pointcloud.point_count = pc.point_count;

      payload.pointcloud.blob_offset = cursor;

      payload.pointcloud.blob_size = size;



      if (!CopyBlob(blob_arena, blob_arena_size, cursor, pc.data.data(), size, error)) {

        return false;

      }

    }

  }



  return true;

}



bool ValidateShmRequest(const ShmHeader* header, const std::uint8_t* blob_arena, std::size_t blob_arena_size,

                        std::string* error) {

  if (header == nullptr) {

    if (error != nullptr) {

      *error = "null shm header";

    }

    return false;

  }

  if (header->magic != kMagic || header->version != kVersion) {

    if (error != nullptr) {

      *error = "invalid shm header version";

    }

    return false;

  }



  if (header->input_mode == static_cast<std::uint32_t>(AlgoInputMode::kOfflinePath)) {

    if (header->session_dir[0] == '\0') {

      if (error != nullptr) {

        *error = "offline mode requires session_dir";

      }

      return false;

    }

    return true;

  }



  for (std::int32_t i = 0; i < header->camera_count; ++i) {

    const auto& payload = header->cameras[i];

    if (HasTransferFlag(header->transfer_flags, AlgoTransferFlag::kDepth) &&

        payload.depth.blob_size > 0) {

      if (payload.depth.blob_offset + payload.depth.blob_size > blob_arena_size) {

        if (error != nullptr) {

          *error = "depth blob out of range";

        }

        return false;

      }

      if (blob_arena == nullptr) {

        if (error != nullptr) {

          *error = "missing blob arena";

        }

        return false;

      }

    }

    if (HasTransferFlag(header->transfer_flags, AlgoTransferFlag::kPointCloud) &&

        payload.pointcloud.blob_size > 0) {

      if (payload.pointcloud.blob_offset + payload.pointcloud.blob_size > blob_arena_size) {

        if (error != nullptr) {

          *error = "pointcloud blob out of range";

        }

        return false;

      }

      if (blob_arena == nullptr) {

        if (error != nullptr) {

          *error = "missing blob arena";

        }

        return false;

      }

    }

  }



  (void)blob_arena;

  return true;

}



}  // namespace visual::shm



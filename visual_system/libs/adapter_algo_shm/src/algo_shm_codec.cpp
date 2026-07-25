/**
 * @file algo_shm_codec.cpp
 * @brief SHM 载荷编解码：视觉侧 WriteRequestToShm / 算法侧 Validate + 结果图写回。
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

bool CopyBlob(std::uint8_t* arena, std::size_t arena_size, std::uint64_t offset,
              const std::uint8_t* src, std::size_t size, std::string* error) {
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

/** 每相机槽位起点：步长由 transfer_flags 决定（未开的模态不占槽）。 */
std::uint64_t CameraBlobBaseOffset(std::size_t camera_index, std::uint32_t transfer_flags) {
  return static_cast<std::uint64_t>(camera_index) * CameraBlobStrideBytes(transfer_flags);
}

/** 图像槽在相机 stride 内的固定起点（depth/pc 预留之后）。 */
std::uint64_t CameraImageSlotOffset(std::size_t camera_index, std::uint32_t transfer_flags) {
  std::uint64_t cursor = CameraBlobBaseOffset(camera_index, transfer_flags);
  if (TransferHasDepth(transfer_flags)) {
    cursor += kMaxDepthBytes;
  }
  if (TransferHasPointCloud(transfer_flags)) {
    cursor += kMaxPointCloudBytes;
  }
  return cursor;
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

  const auto camera_count =
      static_cast<std::int32_t>(std::min(req.captures.size(), static_cast<std::size_t>(kMaxCameras)));
  header->camera_count = camera_count;
  header->input_mode = static_cast<std::uint32_t>(req.input_mode);
  header->transfer_flags = req.transfer_flags;
  header->blob_arena_bytes = static_cast<std::uint64_t>(blob_arena_size);

  std::memset(header->cameras, 0, sizeof(header->cameras));

  if (req.input_mode == AlgoInputMode::kOfflinePath) {
    std::strncpy(header->session_dir, req.session_dir.c_str(), sizeof(header->session_dir) - 1);
    for (std::int32_t i = 0; i < camera_count; ++i) {
      const auto& cap = req.captures[static_cast<std::size_t>(i)];
      std::strncpy(header->cameras[i].camera_serial, cap.camera_serial.c_str(),
                   sizeof(header->cameras[i].camera_serial) - 1);
    }
    return true;
  }

  header->session_dir[0] = '\0';

  for (std::int32_t i = 0; i < camera_count; ++i) {
    const auto& cap = req.captures[static_cast<std::size_t>(i)];
    auto& payload = header->cameras[i];
    std::strncpy(payload.camera_serial, cap.camera_serial.c_str(),
                 sizeof(payload.camera_serial) - 1);

    const std::uint64_t base = CameraBlobBaseOffset(static_cast<std::size_t>(i), req.transfer_flags);
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
      cursor += kMaxDepthBytes;  // 固定槽位步进，与 ImageSlotOffset 一致
    } else if (TransferHasDepth(req.transfer_flags)) {
      cursor += kMaxDepthBytes;
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
      cursor += kMaxPointCloudBytes;
    } else if (TransferHasPointCloud(req.transfer_flags)) {
      cursor += kMaxPointCloudBytes;
    }

    if (HasTransferFlag(req.transfer_flags, AlgoTransferFlag::kGray) && cap.gray &&
        !cap.gray->data.empty()) {
      const auto& gray = *cap.gray;
      const std::size_t expected =
          static_cast<std::size_t>(gray.width) * gray.height;  // Mono8
      if (gray.data.size() < expected) {
        if (error != nullptr) {
          *error = "gray buffer too small";
        }
        return false;
      }
      if (expected > kMaxImageBytes) {
        if (error != nullptr) {
          *error = "gray exceeds shm limit";
        }
        return false;
      }

      payload.image.width = gray.width;
      payload.image.height = gray.height;
      payload.image.format = ImagePixelFormat::kMono8;
      payload.image.bytes_per_pixel = 1;
      payload.image.row_stride_bytes = gray.width;
      payload.image.blob_offset = cursor;
      payload.image.blob_size = expected;

      if (!CopyBlob(blob_arena, blob_arena_size, cursor, gray.data.data(), expected, error)) {
        return false;
      }
    }
  }

  return true;
}

bool ValidateShmRequest(const ShmHeader* header, const std::uint8_t* blob_arena,
                        std::size_t blob_arena_size, std::string* error) {
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
    if (HasTransferFlag(header->transfer_flags, AlgoTransferFlag::kGray) &&
        payload.image.blob_size > 0) {
      if (payload.image.blob_offset + payload.image.blob_size > blob_arena_size) {
        if (error != nullptr) {
          *error = "image blob out of range";
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

bool WriteResultImageToShm(ShmHeader* header, std::uint8_t* blob_arena, std::size_t blob_arena_size,
                           int camera_index, std::uint32_t width, std::uint32_t height,
                           ImagePixelFormat format, const std::uint8_t* pixels, std::size_t byte_count,
                           std::string* error) {
  if (header == nullptr || blob_arena == nullptr || pixels == nullptr || camera_index < 0 ||
      camera_index >= static_cast<int>(kMaxCameras)) {
    if (error != nullptr) {
      *error = "invalid result image args";
    }
    return false;
  }
  if (!TransferHasGray(header->transfer_flags)) {
    if (error != nullptr) {
      *error = "transferGray not enabled";
    }
    return false;
  }
  const std::uint32_t bpp = BytesPerImagePixel(format);
  if (bpp == 0 || width == 0 || height == 0) {
    if (error != nullptr) {
      *error = "unsupported result image format";
    }
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(width) * height * bpp;
  if (byte_count < expected || expected > kMaxImageBytes) {
    if (error != nullptr) {
      *error = "result image size invalid";
    }
    return false;
  }

  const std::uint64_t slot =
      CameraImageSlotOffset(static_cast<std::size_t>(camera_index), header->transfer_flags);
  if (!CopyBlob(blob_arena, blob_arena_size, slot, pixels, expected, error)) {
    return false;
  }

  auto& meta = header->cameras[camera_index].image;
  meta.width = width;
  meta.height = height;
  meta.format = format;
  meta.bytes_per_pixel = bpp;
  meta.row_stride_bytes = width * bpp;
  meta.blob_offset = slot;
  meta.blob_size = expected;
  return true;
}

bool ReadFirstResultImage(const ShmHeader* header, const std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, std::vector<std::uint8_t>* out_pixels,
                          std::uint32_t* out_width, std::uint32_t* out_height,
                          ImagePixelFormat* out_format, std::string* error) {
  if (header == nullptr || blob_arena == nullptr || out_pixels == nullptr || out_width == nullptr ||
      out_height == nullptr || out_format == nullptr) {
    if (error != nullptr) {
      *error = "invalid read result image args";
    }
    return false;
  }
  if (!TransferHasGray(header->transfer_flags)) {
    return false;
  }
  for (std::int32_t i = 0; i < header->camera_count; ++i) {
    const auto& meta = header->cameras[i].image;
    if (meta.blob_size == 0 || meta.width == 0 || meta.height == 0 ||
        meta.format == ImagePixelFormat::kNone) {
      continue;
    }
    if (meta.blob_offset + meta.blob_size > blob_arena_size) {
      if (error != nullptr) {
        *error = "result image out of range";
      }
      return false;
    }
    out_pixels->assign(blob_arena + meta.blob_offset, blob_arena + meta.blob_offset + meta.blob_size);
    *out_width = meta.width;
    *out_height = meta.height;
    *out_format = meta.format;
    return true;
  }
  return false;
}

}  // namespace visual::shm

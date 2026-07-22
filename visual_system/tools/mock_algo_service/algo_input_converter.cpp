/**
 * @file algo_input_converter.cpp
 * @brief 算法侧输入校验；可选将深度/点云落盘便于排查。
 */
#include "algo_input_converter.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "algo_log.h"
#include "visual/algo_shm_codec.h"
#include "visual/capture_data_format.h"

namespace algo {
namespace {

std::string MakeTimestampPrefix() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t t = clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
  return oss.str();
}

bool WritePointCloudPlyFile(const std::filesystem::path& path, const std::uint8_t* data,
                            std::uint64_t point_count, visual::PointCloudFormat format) {
  if (data == nullptr || point_count == 0) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "ply\nformat binary_little_endian 1.0\nelement vertex " << point_count << '\n';
  if (format == visual::PointCloudFormat::kXyzFloat32Mm) {
    out << "property float x\nproperty float y\nproperty float z\nend_header\n";
    const std::size_t bytes = static_cast<std::size_t>(point_count) * 3 * sizeof(float);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
  } else {
    out << "property double x\nproperty double y\nproperty double z\nend_header\n";
    const std::size_t bytes = static_cast<std::size_t>(point_count) * 3 * sizeof(double);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
  }
  return out.good();
}

bool WriteDepthRawFile(const std::filesystem::path& path, const std::uint8_t* data,
                       std::uint32_t width, std::uint32_t height, std::uint64_t blob_size,
                       visual::DepthPixelFormat format) {
  if (data == nullptr || width == 0 || height == 0 || blob_size == 0) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(&width), sizeof(width));
  out.write(reinterpret_cast<const char*>(&height), sizeof(height));
  const std::uint32_t fmt = static_cast<std::uint32_t>(format);
  out.write(reinterpret_cast<const char*>(&fmt), sizeof(fmt));
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(blob_size));
  return out.good();
}

}  // namespace

bool PrepareAlgoInputFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                             std::size_t blob_arena_size, const AlgoConfig& config,
                             const std::filesystem::path& exe_dir, std::string* error) {
  if (!visual::shm::ValidateShmRequest(header, blob_arena, blob_arena_size, error)) {
    return false;
  }

  const std::string stamp = MakeTimestampPrefix();
  const auto data_dir = exe_dir / "data";

  for (std::int32_t i = 0; i < header->camera_count; ++i) {
    const auto& cam = header->cameras[i];
    const std::string serial =
        cam.camera_serial[0] != '\0' ? cam.camera_serial : ("cam" + std::to_string(i));

    if (visual::HasTransferFlag(header->transfer_flags, visual::AlgoTransferFlag::kDepth) &&
        cam.depth.blob_size > 0) {
      {
        std::ostringstream oss;
        oss << "收到深度 " << serial << " " << cam.depth.width << "x" << cam.depth.height;
        AlgoDebug(oss.str());
      }
      if (config.debug_save_depth) {
        const auto depth_path = data_dir / (stamp + "_" + serial + "_depth.raw");
        const auto* blob = blob_arena + cam.depth.blob_offset;
        if (WriteDepthRawFile(depth_path, blob, cam.depth.width, cam.depth.height, cam.depth.blob_size,
                              cam.depth.format)) {
          AlgoDebug("已保存深度: " + depth_path.string());
        } else {
          AlgoError("保存深度失败: " + depth_path.string());
        }
      }
    }
    if (visual::HasTransferFlag(header->transfer_flags, visual::AlgoTransferFlag::kPointCloud) &&
        cam.pointcloud.blob_size > 0) {
      {
        std::ostringstream oss;
        oss << "收到点云 " << serial << " 点数=" << cam.pointcloud.point_count;
        AlgoDebug(oss.str());
      }
      if (config.debug_save_pointcloud) {
        const auto ply_path = data_dir / (stamp + "_" + serial + "_pointcloud.ply");
        const auto* blob = blob_arena + cam.pointcloud.blob_offset;
        if (WritePointCloudPlyFile(ply_path, blob, cam.pointcloud.point_count,
                                   static_cast<visual::PointCloudFormat>(cam.pointcloud.format))) {
          AlgoDebug("已保存点云: " + ply_path.string());
        } else {
          AlgoError("保存点云失败: " + ply_path.string());
        }
      }
    }
  }
  return true;
}

bool PrepareAlgoInputFromPaths(const visual::shm::ShmHeader* header, std::string* error) {
  if (header == nullptr) {
    if (error != nullptr) {
      *error = "请求无效";
    }
    return false;
  }
  if (header->session_dir[0] == '\0') {
    if (error != nullptr) {
      *error = "回放目录为空";
    }
    return false;
  }

  AlgoDebug(std::string("离线回放目录: ") + header->session_dir);
  return true;
}

}  // namespace algo

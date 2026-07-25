/**
 * @file capture_data_format.h
 * @brief 视觉→算法传输的深度图/点云/图像格式与模式开关（SHM 与内存采集共用）。
 *
 * 算法侧根据 DepthPixelFormat / PointCloudFormat / ImagePixelFormat 解析 SHM blob。
 * AlgoTransferFlag 由 setting.json → algo.transferDepth / transferPointcloud / transferGray 映射。
 */
#pragma once

#include <cstdint>

namespace visual {

/** 深度图像素格式。算法侧根据此字段解析 blob。 */
enum class DepthPixelFormat : std::uint32_t {
  kNone = 0,
  kUint16Mm = 1,   /**< Stub 仿真；每像素 uint16 毫米 */
  kFloat32Mm = 2,  /**< 每像素 float32 毫米 */
  kFloat64Mm = 3,  /**< 枚举名历史遗留：RVC GetDataConstPtr 实为米；TIFF(is_m=false) 为毫米。算法侧对 SHM 路径会 ×1000。 */
};

/** 点云数据布局。 */
enum class PointCloudFormat : std::uint32_t {
  kNone = 0,
  kXyzFloat32Mm = 1, /**< 连续 float32 x,y,z（毫米） */
  kXyzFloat64Mm = 2, /**< RVC PointMap 原生 float64 x,y,z（毫米） */
};

/** 可视化图像像素格式（请求多为 Mono8；算法回传可能为 BGR）。 */
enum class ImagePixelFormat : std::uint32_t {
  kNone = 0,
  kMono8 = 1,  /**< 每像素 1 字节灰度 */
  kBgr8 = 2,   /**< 每像素 3 字节，OpenCV BGR 顺序 */
  kRgb8 = 3,   /**< 每像素 3 字节，RGB 顺序 */
  kBgra8 = 4,
  kRgba8 = 5,
};

inline std::uint32_t BytesPerImagePixel(ImagePixelFormat fmt) {
  switch (fmt) {
    case ImagePixelFormat::kMono8:
      return 1;
    case ImagePixelFormat::kBgr8:
    case ImagePixelFormat::kRgb8:
      return 3;
    case ImagePixelFormat::kBgra8:
    case ImagePixelFormat::kRgba8:
      return 4;
    default:
      return 0;
  }
}

/** 控制在线 SHM 是否拷贝对应模态。 */
enum class AlgoTransferFlag : std::uint32_t {
  kNone = 0,
  kDepth = 1u << 0,
  kPointCloud = 1u << 1,
  kGray = 1u << 2,
};

inline AlgoTransferFlag operator|(AlgoTransferFlag a, AlgoTransferFlag b) {
  return static_cast<AlgoTransferFlag>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline AlgoTransferFlag operator&(AlgoTransferFlag a, AlgoTransferFlag b) {
  return static_cast<AlgoTransferFlag>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

inline bool HasTransferFlag(std::uint32_t flags, AlgoTransferFlag bit) {
  return (flags & static_cast<std::uint32_t>(bit)) != 0;
}

/** 算法输入模式。 */
enum class AlgoInputMode : std::uint32_t {
  kOnlineShm = 0,    /**< SHM 直传像素 */
  kOfflinePath = 1,  /**< 历史目录回放 */
};

}  // namespace visual

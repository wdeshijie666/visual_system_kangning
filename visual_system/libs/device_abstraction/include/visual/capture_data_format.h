/**

 * @file capture_data_format.h

 * @brief 视觉→算法传输的深度图/点云格式与模式开关（SHM 与内存采集共用）。

 *

 * 算法侧根据 DepthPixelFormat / PointCloudFormat 解析 SHM blob（见 ShmDepthMeta）。

 * AlgoTransferFlag 由 setting.json → algo.transferDepth / transferPointcloud 映射。

 * AlgoInputMode 区分在线 SHM 直传 vs 历史回放路径模式。

 *

 * 详见 docs/框架流程通路.md §6.2、§8

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



/** 控制在线 SHM 是否拷贝对应模态（setting.json → transferDepth / transferPointcloud）。 */

enum class AlgoTransferFlag : std::uint32_t {

  kNone = 0,

  kDepth = 1u << 0,

  kPointCloud = 1u << 1,

};



inline AlgoTransferFlag operator|(AlgoTransferFlag a, AlgoTransferFlag b) {

  return static_cast<AlgoTransferFlag>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));

}



inline bool HasTransferFlag(std::uint32_t flags, AlgoTransferFlag bit) {

  return (flags & static_cast<std::uint32_t>(bit)) != 0;

}



/** 算法输入模式：在线 SHM 直传 vs 离线路径回放。 */

enum class AlgoInputMode : std::uint32_t {

  kOnlineShm = 0,   /**< 产线 / 离线测试实时采图 */

  kOfflinePath = 1, /**< 历史数据回放，算法从 session_dir 读盘 */

};



}  // namespace visual



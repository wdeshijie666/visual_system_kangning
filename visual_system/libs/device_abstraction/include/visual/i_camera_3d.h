/**
 * @file i_camera_3d.h
 * @brief 3D 相机抽象（RVC 或其它品牌可替换）。
 *
 * 产线周期：Capture() 采到内存供 SHM；SaveLastCaptureToDir() 落盘深度图
 * （实机 RVC::DepthMap::SaveDepthMap → .tif；仿真 → .pgm）。
 * 实现：libs/adapter_rvc/src/rvc_camera_adapter.cpp
 */
#pragma once

#include <memory>
#include <string>

#include "visual/camera_recipe.h"
#include "visual/station_types.h"

namespace visual {

struct CameraInfo {
  std::string id;
  std::string serial;
  std::string ip;
  bool connected = false;
  /** true：内存 Stub（仿真或 STUB_ 序列号），不会驱动真实投影/采集。 */
  bool is_stub = false;
};

/** 控制 Capture() 是否把深度/点云/灰度拷到堆（由 transfer + 落盘开关决定）。 */
struct CaptureCopyOptions {
  bool copy_depth = true;
  bool copy_pointcloud = true;
  bool copy_gray = true;
};

/** 轻量探活结果（不采图、不写参）。 */
enum class CameraProbeResult {
  kAlive = 0,
  kDead = 1,
  kBusy = 2,
};

class ICamera3D {
 public:
  virtual ~ICamera3D() = default;

  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  /**
   * 周期性探活：读 SDK 状态（如 IsOpen / IsPhysicallyConnected），不触发 Capture。
   * 实现须 try_lock 内部互斥：采图中返回 kBusy，避免抢锁影响产线/手动周期。
   */
  virtual CameraProbeResult ProbeAlive() = 0;

  virtual CameraInfo GetInfo() const = 0;

  /** 采集到内存（不落盘）。按 opts 决定是否拷贝深度/点云/灰度。 */
  virtual CaptureBundle Capture(const CaptureCopyOptions& opts = {}) = 0;

  /**
   * 将最近一次 Capture() 的结果写入 session_dir。
   * 必须在同一次 Capture 之后、下一次 Capture 之前调用（实机依赖 SDK 帧缓冲仍有效）。
   * 成功时更新 bundle 内 rgb/depth/pointcloud 路径。
   */
  virtual bool SaveLastCaptureToDir(const std::string& session_dir, const std::string& prefix,
                                    CaptureBundle* bundle) = 0;

  /** 同步采集并写盘（兼容/测试）。 */
  virtual CaptureBundle CaptureToDir(const std::string& session_dir, const std::string& prefix) = 0;

  /**
   * 导入 RVC Manager 导出的配方（.json）。
   * @param out_params 非空时填充中文参数表，供 UI 显示。
   */
  virtual bool LoadRecipeFile(const std::string& file_path, RecipeParamList* out_params = nullptr) = 0;
};

using CameraPtr = std::shared_ptr<ICamera3D>;

}  // namespace visual

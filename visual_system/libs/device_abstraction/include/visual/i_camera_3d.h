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
};

class ICamera3D {
 public:
  virtual ~ICamera3D() = default;

  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  virtual CameraInfo GetInfo() const = 0;

  /** 采集到内存（不落盘），供在线 SHM 传输。 */
  virtual CaptureBundle Capture() = 0;

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

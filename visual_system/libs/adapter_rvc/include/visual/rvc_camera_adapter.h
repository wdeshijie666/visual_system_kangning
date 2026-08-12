/**
 * @file rvc_camera_adapter.h
 * @brief RVC SDK 相机封装；仿真/无 SDK 时使用 Stub。
 */
#pragma once

#include <string>

#include "visual/i_camera_3d.h"

namespace visual {

struct StubCameraOptions {
  int image_width = 2448;
  int image_height = 2048;
  bool solid_black = true;
  /**
   * 仿真专用：相对 SmartGuide.exe 目录的深度 TIFF 文件名。
   * 非空时每次 Capture 固定读该文件写入深度并走 SHM；读失败则本次 Capture 失败（不静默假深度）。
   * 空字符串则退回合成深度。真机 RvcCamera 忽略本字段。
   */
  std::string sim_depth_tiff = "sim_test.tiff";
};

/** @param force_stub true 时强制 Stub（仿真模式），否则有 SDK 则走真机。 */
CameraPtr CreateRvcCamera(const std::string& id, const std::string& serial, bool force_stub = false,
                          const StubCameraOptions& stub_options = {});

}  // namespace visual

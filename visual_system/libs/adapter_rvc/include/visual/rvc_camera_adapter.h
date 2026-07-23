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
};

/** @param force_stub true 时强制 Stub（仿真模式），否则有 SDK 则走真机。 */
CameraPtr CreateRvcCamera(const std::string& id, const std::string& serial, bool force_stub = false,
                          const StubCameraOptions& stub_options = {});

}  // namespace visual

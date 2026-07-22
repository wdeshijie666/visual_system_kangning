/**
 * @file rvc_probe.cpp
 * @brief Diagnose RVC Capture failure (Normal Collect Failed).
 */
#include <cstdio>

#include "RVC/RVC.h"

static bool TryOnce(RVC::X2& cam, RVC::CaptureMode mode) {
  RVC::X2::CaptureOptions opts;
  opts.capture_mode = mode;
  opts.trigger_mode = RVC::TriggerMode_SoftWare;
  const bool ok = cam.Capture(opts);
  std::printf("Capture mode=%d => %d err=%d msg=%s\n", static_cast<int>(mode), ok ? 1 : 0,
              RVC::GetLastError(),
              RVC::GetLastErrorMessage() ? RVC::GetLastErrorMessage() : "(null)");
  if (!ok) {
    return false;
  }
  RVC::DepthMap depth = cam.GetDepthMap();
  std::printf("  Depth valid=%d size=%dx%d\n", depth.IsValid() ? 1 : 0,
              depth.IsValid() ? depth.GetSize().cols : 0,
              depth.IsValid() ? depth.GetSize().rows : 0);
  return depth.IsValid() && depth.GetDataConstPtr() != nullptr;
}

int main(int argc, char* argv[]) {
  const char* want = (argc > 1) ? argv[1] : "G2GM150B660";
  std::printf("probe sn=%s\n", want);
  if (!RVC::SystemInit()) {
    return 1;
  }

  RVC::Device found = RVC::SystemFindDevice(want);
  if (!found.IsValid()) {
    RVC::SystemShutdown();
    return 1;
  }

  RVC::DeviceInfo info;
  if (found.GetDeviceInfo(&info)) {
    std::printf("name=%s sn=%s support=0x%x\n", info.name, info.sn,
                static_cast<unsigned>(info.support_capture_mode));
  }

  auto cam = RVC::X2::Create(found);
  if (!cam.IsValid() || !cam.Open()) {
    std::printf("Open fail err=%d\n", RVC::GetLastError());
    RVC::Device::Destroy(found);
    RVC::SystemShutdown();
    return 2;
  }
  std::printf("Open OK\n");

  int exit_code = 4;
  const RVC::CaptureMode modes[] = {RVC::CaptureMode_Fast, RVC::CaptureMode_Normal,
                                    RVC::CaptureMode_Ultra, RVC::CaptureMode_AntiInterReflection};
  for (RVC::CaptureMode m : modes) {
    if ((info.support_capture_mode & m) == 0) {
      std::printf("skip unsupported mode=%d\n", static_cast<int>(m));
      continue;
    }
    if (TryOnce(cam, m)) {
      exit_code = 0;
      break;
    }
  }

  cam.Close();
  RVC::X2::Destroy(cam);
  RVC::Device::Destroy(found);
  RVC::SystemShutdown();
  std::printf("done exit=%d\n", exit_code);
  return exit_code;
}

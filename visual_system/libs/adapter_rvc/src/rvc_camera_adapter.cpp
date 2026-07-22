/**
 * @file rvc_camera_adapter.cpp
 * @brief RVC 相机适配：Capture() 内存采集（阶段 6.2），仿真 Stub / 实机 RVC SDK。
 * 详见 docs/框架流程通路.md §4.1、§6.2
 */
#include "visual/rvc_camera_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace fs = std::filesystem;

#ifdef VISUAL_HAS_RVC_SDK
#include <RVC/RVC.h>
#endif

namespace visual {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 1) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), n);
  return wide;
}

fs::path PathFromUtf8(const std::string& utf8) {
  return fs::path(Utf8ToWide(utf8));
}

std::ofstream OpenBinaryOut(const std::string& utf8_path) {
  return std::ofstream(Utf8ToWide(utf8_path).c_str(), std::ios::binary);
}
#else
fs::path PathFromUtf8(const std::string& utf8) {
  return fs::path(utf8);
}

std::ofstream OpenBinaryOut(const std::string& utf8_path) {
  return std::ofstream(utf8_path, std::ios::binary);
}
#endif

void AppendRecipeParam(RecipeParamList* out, const char* group_zh, const char* name_zh,
                       const std::string& value) {
  if (out == nullptr) {
    return;
  }
  RecipeParamItem item;
  item.group_zh = group_zh;
  item.name_zh = name_zh;
  item.value = value;
  out->push_back(std::move(item));
}

std::string FormatBoolZh(bool v) { return v ? "是" : "否"; }

std::string FormatFloat(double v, int precision = 3) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << v;
  std::string s = oss.str();
  while (s.size() > 1 && s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) {
    const bool drop_dot = s.back() == '.';
    s.pop_back();
    if (drop_dot) {
      break;
    }
  }
  return s;
}

std::optional<std::string> ExtractJsonRawValue(const std::string& text, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  const auto key_pos = text.find(pat);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const auto colon = text.find(':', key_pos + pat.size());
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  std::size_t i = colon + 1;
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
    ++i;
  }
  if (i >= text.size()) {
    return std::nullopt;
  }
  if (text[i] == '"') {
    const auto end = text.find('"', i + 1);
    if (end == std::string::npos) {
      return std::nullopt;
    }
    return text.substr(i + 1, end - i - 1);
  }
  std::size_t j = i;
  while (j < text.size() && text[j] != ',' && text[j] != '}' && text[j] != '\n' && text[j] != '\r') {
    ++j;
  }
  std::string raw = text.substr(i, j - i);
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) {
    raw.pop_back();
  }
  return raw;
}

std::string ProjectorColorZhFromInt(int color) {
  switch (color) {
    case 1:
      return "红";
    case 2:
      return "绿";
    case 4:
      return "蓝";
    case 8:
      return "白";
    case 15:
      return "全色";
    default:
      return std::to_string(color);
  }
}

std::string CaptureModeZhFromInt(int mode) {
  switch (mode) {
    case 1:
      return "快速";
    case 2:
      return "标准";
    case 4:
      return "超精细";
    case 8:
      return "稳健";
    case 16:
      return "抗互反射";
    case 32:
      return "摆动线扫";
    case 64:
      return "固定线扫";
    case 128:
      return "线阵移位";
    default:
      return std::to_string(mode);
  }
}

/** 从 RVC Manager 导出的 JSON 文本填充中文参数表（仿真 / SDK 回退共用）。 */
bool FillRecipeParamsFromJsonText(const std::string& text, RecipeParamList* out) {
  if (out == nullptr) {
    return false;
  }
  out->clear();

  auto add_str = [&](const char* group, const char* name, const std::string& key) {
    if (auto v = ExtractJsonRawValue(text, key)) {
      AppendRecipeParam(out, group, name, *v);
    }
  };
  auto add_bool = [&](const char* group, const char* name, const std::string& key) {
    if (auto v = ExtractJsonRawValue(text, key)) {
      AppendRecipeParam(out, group, name, (*v == "true" || *v == "1") ? "是" : "否");
    }
  };
  auto add_enum_projector = [&](const char* group, const char* name, const std::string& key) {
    if (auto v = ExtractJsonRawValue(text, key)) {
      AppendRecipeParam(out, group, name, ProjectorColorZhFromInt(std::atoi(v->c_str())));
    }
  };
  auto add_enum_mode = [&](const char* group, const char* name, const std::string& key) {
    if (auto v = ExtractJsonRawValue(text, key)) {
      AppendRecipeParam(out, group, name, CaptureModeZhFromInt(std::atoi(v->c_str())));
    }
  };

  add_str("拍摄参数", "2D曝光时间(ms)", "exposure_time_2d");
  add_str("拍摄参数", "3D曝光时间(ms)", "exposure_time_3d");
  add_str("拍摄参数", "2D增益", "gain_2d");
  add_str("拍摄参数", "3D增益", "gain_3d");
  add_str("拍摄参数", "2D伽马", "gamma_2d");
  add_str("拍摄参数", "3D伽马", "gamma_3d");
  add_str("拍摄参数", "投影亮度", "projector_brightness");
  add_enum_projector("拍摄参数", "投影颜色", "projector_color");
  add_bool("拍摄参数", "2D采集使用投影仪", "use_projector_capturing_2d_image");
  add_str("拍摄参数", "扫描次数", "scan_times");
  add_str("拍摄参数", "光对比度阈值", "light_contrast_threshold");
  add_str("拍摄参数", "置信度阈值", "confidence_threshold");
  add_enum_mode("拍摄参数", "采集模式", "capture_mode");

  add_str("HDR参数", "HDR额外次数", "hdr_exposure_times");
  add_str("HDR参数", "HDR曝光1(ms)", "hdr_exposuretime_content_1");
  add_str("HDR参数", "HDR曝光2(ms)", "hdr_exposuretime_content_2");
  add_str("HDR参数", "HDR曝光3(ms)", "hdr_exposuretime_content_3");
  add_str("HDR参数", "HDR增益1", "hdr_gain_3d_1");
  add_str("HDR参数", "HDR增益2", "hdr_gain_3d_2");
  add_str("HDR参数", "HDR增益3", "hdr_gain_3d_3");
  add_str("HDR参数", "HDR投影亮度1", "hdr_projector_brightness_1");
  add_str("HDR参数", "HDR投影亮度2", "hdr_projector_brightness_2");
  add_str("HDR参数", "HDR投影亮度3", "hdr_projector_brightness_3");
  add_str("HDR参数", "HDR扫描次数1", "hdr_scan_times_1");
  add_str("HDR参数", "HDR扫描次数2", "hdr_scan_times_2");
  add_str("HDR参数", "HDR扫描次数3", "hdr_scan_times_3");

  add_bool("后处理参数", "自动去噪", "use_auto_noise_removal");
  add_str("后处理参数", "去噪距离(mm)", "noise_removal_distance");
  add_str("后处理参数", "去噪点数阈值", "noise_removal_point_number");
  add_str("后处理参数", "平滑系数", "smooth_sigma");
  add_str("后处理参数", "下采样距离", "downsample_distance");
  add_bool("后处理参数", "点云补全", "pointcloud_completion");
  add_str("后处理参数", "反射滤波阈值", "reflection_filter_threshold");
  add_str("后处理参数", "Z截断最小(mm)", "truncate_z_min");
  add_str("后处理参数", "Z截断最大(mm)", "truncate_z_max");
  add_bool("后处理参数", "计算法线", "calc_normal");
  add_str("后处理参数", "法线半径", "calc_normal_radius");
  add_bool("后处理参数", "自动双边滤波", "use_auto_bilateral_filter");

  return !out->empty();
}

bool FillRecipeParamsFromJsonFile(const std::string& utf8_path, RecipeParamList* out) {
  std::ifstream in(PathFromUtf8(utf8_path), std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return FillRecipeParamsFromJsonText(ss.str(), out);
}

bool WriteBinaryPointCloudPly(const std::string& utf8_path, const PointCloudBuffer& pc) {
  if (pc.format != PointCloudFormat::kXyzFloat64Mm || pc.point_count == 0) {
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(pc.point_count) * 3 * sizeof(double);
  if (pc.data.size() < expected) {
    return false;
  }
  std::ofstream out = OpenBinaryOut(utf8_path);
  if (!out) {
    return false;
  }
  out << "ply\nformat binary_little_endian 1.0\nelement vertex " << pc.point_count
      << "\nproperty double x\nproperty double y\nproperty double z\nend_header\n";
  out.write(reinterpret_cast<const char*>(pc.data.data()), static_cast<std::streamsize>(expected));
  return out.good();
}

bool WriteFloat64DepthRawFile(const std::string& utf8_path, const DepthImageBuffer& depth) {
  if (depth.format != DepthPixelFormat::kFloat64Mm || depth.width == 0 || depth.height == 0) {
    return false;
  }
  const std::size_t expected =
      static_cast<std::size_t>(depth.width) * depth.height * sizeof(double);
  if (depth.data.size() < expected) {
    return false;
  }
  std::ofstream out = OpenBinaryOut(utf8_path);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(&depth.width), sizeof(depth.width));
  out.write(reinterpret_cast<const char*>(&depth.height), sizeof(depth.height));
  out.write(reinterpret_cast<const char*>(depth.data.data()), static_cast<std::streamsize>(expected));
  return out.good();
}

bool WriteBlackGrayscalePgm(const fs::path& path, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "P5\n" << width << " " << height << "\n255\n";
  const std::vector<unsigned char> row(static_cast<std::size_t>(width), 0);
  for (int y = 0; y < height; ++y) {
    out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
  }
  return out.good();
}

bool WritePatternGrayscalePgm(const fs::path& path, int width, int height) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "P5\n" << width << " " << height << "\n255\n";
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto v = static_cast<unsigned char>((x * 2 + y * 3) % 256);
      out.put(static_cast<char>(v));
    }
  }
  return out.good();
}

bool WriteBlackGrayscaleBmp(const fs::path& path, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }

  const int row_stride = ((width + 3) / 4) * 4;
  const std::uint32_t pixel_data_size = static_cast<std::uint32_t>(row_stride * height);
  const std::uint32_t file_size = 14 + 40 + 256 * 4 + pixel_data_size;

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  auto write_u16 = [&](std::uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
  };
  auto write_u32 = [&](std::uint32_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
    out.put(static_cast<char>((v >> 16) & 0xFF));
    out.put(static_cast<char>((v >> 24) & 0xFF));
  };

  out.put('B');
  out.put('M');
  write_u32(file_size);
  write_u16(0);
  write_u16(0);
  write_u32(14 + 40 + 256 * 4);
  write_u32(40);
  write_u32(static_cast<std::uint32_t>(width));
  write_u32(static_cast<std::uint32_t>(height));
  write_u16(1);
  write_u16(8);
  write_u32(0);
  write_u32(pixel_data_size);
  write_u32(2835);
  write_u32(2835);
  write_u32(256);
  write_u32(256);

  for (int i = 0; i < 256; ++i) {
    out.put(static_cast<char>(i));
    out.put(static_cast<char>(i));
    out.put(static_cast<char>(i));
    out.put(static_cast<char>(0));
  }

  const std::vector<unsigned char> row(static_cast<std::size_t>(row_stride), 0);
  for (int y = 0; y < height; ++y) {
    out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
  }
  return out.good();
}

bool WriteStubPointCloudPly(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "ply\n"
         "format ascii 1.0\n"
         "element vertex 4\n"
         "property float x\n"
         "property float y\n"
         "property float z\n"
         "end_header\n"
         "0 0 0\n"
         "1 0 0\n"
         "0 1 0\n"
         "0 0 1\n";
  return out.good();
}

std::shared_ptr<DepthImageBuffer> MakeStubDepthBuffer(int width, int height, bool solid_black) {
  auto depth = std::make_shared<DepthImageBuffer>();
  depth->width = static_cast<std::uint32_t>(width);
  depth->height = static_cast<std::uint32_t>(height);
  depth->format = DepthPixelFormat::kUint16Mm;
  depth->data.resize(static_cast<std::size_t>(width) * height * 2);
  auto* pixels = reinterpret_cast<std::uint16_t*>(depth->data.data());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::uint16_t v =
          solid_black ? 0 : static_cast<std::uint16_t>((x * 2 + y * 3) % 65535);
      pixels[y * width + x] = v;
    }
  }
  return depth;
}

std::shared_ptr<GrayImageBuffer> MakeStubGrayBuffer(int width, int height, bool solid_black) {
  auto gray = std::make_shared<GrayImageBuffer>();
  gray->width = static_cast<std::uint32_t>(width);
  gray->height = static_cast<std::uint32_t>(height);
  gray->data.resize(static_cast<std::size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      gray->data[static_cast<std::size_t>(y * width + x)] =
          solid_black ? 0 : static_cast<std::uint8_t>((x + y * 3) % 256);
    }
  }
  return gray;
}

std::shared_ptr<PointCloudBuffer> MakeStubPointCloudBuffer() {
  auto pc = std::make_shared<PointCloudBuffer>();
  pc->format = PointCloudFormat::kXyzFloat64Mm;
  pc->point_count = 4;
  pc->data.resize(4 * 3 * sizeof(double));
  const double pts[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::memcpy(pc->data.data(), pts, sizeof(pts));
  return pc;
}

class StubRvcCamera final : public ICamera3D {
 public:
  StubRvcCamera(std::string id, std::string serial, StubCameraOptions options)
      : id_(std::move(id)), serial_(std::move(serial)), options_(options) {}

  bool Connect() override {
    connected_ = true;
    return true;
  }
  void Disconnect() override { connected_ = false; }
  bool IsConnected() const override { return connected_; }

  CameraInfo GetInfo() const override {
    CameraInfo info;
    info.id = id_;
    info.serial = serial_;
    info.ip = "stub";
    info.connected = connected_;
    info.is_stub = true;
    return info;
  }

  /** 仿真：生成 uint16 深度 + Mono8 灰度 + 点云；落盘由 dataStub 开关控制（经 SaveCaptureBundleToDir）。 */
  CaptureBundle Capture() override {
    CaptureBundle bundle;
    bundle.camera_serial = serial_;
    bundle.depth = MakeStubDepthBuffer(options_.image_width, options_.image_height, options_.solid_black);
    bundle.gray = MakeStubGrayBuffer(options_.image_width, options_.image_height, options_.solid_black);
    bundle.pointcloud = MakeStubPointCloudBuffer();
    bundle.ok = bundle.depth != nullptr;
    return bundle;
  }

  bool SaveLastCaptureToDir(const std::string& session_dir, const std::string& prefix,
                            CaptureBundle* bundle) override {
    // CaptureToDir 兼容路径：默认只写深度图（与 dataStub 默认一致）
    if (bundle == nullptr || !bundle->ok || !bundle->depth) {
      return false;
    }
    fs::create_directories(session_dir);
    bundle->rgb_path.clear();
    // 不清理 pointcloud_path：点云仍可由 dataStub.savePointcloud + 异步队列落盘
    bundle->depth_path = (fs::path(session_dir) / (prefix + "_depth.pgm")).string();
    return options_.solid_black
               ? WriteBlackGrayscalePgm(bundle->depth_path, options_.image_width, options_.image_height)
               : WritePatternGrayscalePgm(bundle->depth_path, options_.image_width,
                                          options_.image_height);
  }

  CaptureBundle CaptureToDir(const std::string& session_dir, const std::string& prefix) override {
    CaptureBundle bundle = Capture();
    if (!SaveLastCaptureToDir(session_dir, prefix, &bundle)) {
      bundle.ok = false;
    }
    return bundle;
  }

  bool LoadRecipeFile(const std::string& file_path, RecipeParamList* out_params = nullptr) override {
    if (!fs::exists(PathFromUtf8(file_path))) {
      return false;
    }
    if (out_params != nullptr) {
      FillRecipeParamsFromJsonFile(file_path, out_params);
    }
    return true;
  }

 private:
  std::string id_;
  std::string serial_;
  StubCameraOptions options_;
  bool connected_ = false;
};

#ifdef VISUAL_HAS_RVC_SDK

void FillRecipeParamsFromCaptureOptions(const RVC::X2::CaptureOptions& opts, RecipeParamList* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();
  AppendRecipeParam(out, "拍摄参数", "2D曝光时间(ms)", std::to_string(opts.exposure_time_2d));
  AppendRecipeParam(out, "拍摄参数", "3D曝光时间(ms)", std::to_string(opts.exposure_time_3d));
  AppendRecipeParam(out, "拍摄参数", "2D增益", FormatFloat(opts.gain_2d));
  AppendRecipeParam(out, "拍摄参数", "3D增益", FormatFloat(opts.gain_3d));
  AppendRecipeParam(out, "拍摄参数", "2D伽马", FormatFloat(opts.gamma_2d));
  AppendRecipeParam(out, "拍摄参数", "3D伽马", FormatFloat(opts.gamma_3d));
  AppendRecipeParam(out, "拍摄参数", "投影亮度", std::to_string(opts.projector_brightness));
  AppendRecipeParam(out, "拍摄参数", "投影颜色",
                    ProjectorColorZhFromInt(static_cast<int>(opts.projector_color)));
  AppendRecipeParam(out, "拍摄参数", "2D采集使用投影仪",
                    FormatBoolZh(opts.use_projector_capturing_2d_image));
  AppendRecipeParam(out, "拍摄参数", "扫描次数", std::to_string(opts.scan_times));
  AppendRecipeParam(out, "拍摄参数", "光对比度阈值", std::to_string(opts.light_contrast_threshold));
  AppendRecipeParam(out, "拍摄参数", "置信度阈值", FormatFloat(opts.confidence_threshold));
  AppendRecipeParam(out, "拍摄参数", "采集模式",
                    CaptureModeZhFromInt(static_cast<int>(opts.capture_mode)));

  AppendRecipeParam(out, "HDR参数", "HDR额外次数", std::to_string(opts.hdr_exposure_times));
  for (int i = 0; i < 3; ++i) {
    const std::string idx = std::to_string(i + 1);
    AppendRecipeParam(out, "HDR参数", ("HDR曝光" + idx + "(ms)").c_str(),
                      std::to_string(opts.hdr_exposuretime_content[i]));
    AppendRecipeParam(out, "HDR参数", ("HDR增益" + idx).c_str(), FormatFloat(opts.hdr_gain_3d[i]));
    AppendRecipeParam(out, "HDR参数", ("HDR投影亮度" + idx).c_str(),
                      std::to_string(opts.hdr_projector_brightness[i]));
    AppendRecipeParam(out, "HDR参数", ("HDR扫描次数" + idx).c_str(),
                      std::to_string(opts.hdr_scan_times[i]));
  }

  AppendRecipeParam(out, "后处理参数", "自动去噪", FormatBoolZh(opts.use_auto_noise_removal));
  AppendRecipeParam(out, "后处理参数", "去噪距离(mm)", FormatFloat(opts.noise_removal_distance));
  AppendRecipeParam(out, "后处理参数", "去噪点数阈值", std::to_string(opts.noise_removal_point_number));
  AppendRecipeParam(out, "后处理参数", "平滑系数", FormatFloat(opts.smooth_sigma));
  AppendRecipeParam(out, "后处理参数", "下采样距离", FormatFloat(opts.downsample_distance));
  AppendRecipeParam(out, "后处理参数", "点云补全", FormatBoolZh(opts.pointcloud_completion));
  AppendRecipeParam(out, "后处理参数", "反射滤波阈值",
                    std::to_string(opts.reflection_filter_threshold));
  AppendRecipeParam(out, "后处理参数", "Z截断最小(mm)", FormatFloat(opts.truncate_z_min));
  AppendRecipeParam(out, "后处理参数", "Z截断最大(mm)", FormatFloat(opts.truncate_z_max));
  AppendRecipeParam(out, "后处理参数", "计算法线", FormatBoolZh(opts.calc_normal));
  AppendRecipeParam(out, "后处理参数", "法线半径", std::to_string(opts.calc_normal_radius));
  AppendRecipeParam(out, "后处理参数", "自动双边滤波", FormatBoolZh(opts.use_auto_bilateral_filter));
}

std::mutex& RvcSystemMutex() {
  static std::mutex mtx;
  return mtx;
}

int& RvcSystemInitCount() {
  static int count = 0;
  return count;
}

bool EnsureRvcSystemInit() {
  std::lock_guard<std::mutex> lock(RvcSystemMutex());
  if (RvcSystemInitCount() == 0) {
    if (!RVC::SystemInit()) {
      return false;
    }
  }
  ++RvcSystemInitCount();
  return true;
}

void ReleaseRvcSystemInit() {
  std::lock_guard<std::mutex> lock(RvcSystemMutex());
  if (RvcSystemInitCount() <= 0) {
    return;
  }
  --RvcSystemInitCount();
  if (RvcSystemInitCount() == 0) {
    RVC::SystemShutdown();
  }
}

class RvcCamera final : public ICamera3D {
 public:
  RvcCamera(std::string id, std::string serial) : id_(std::move(id)), serial_(std::move(serial)) {}

  ~RvcCamera() override { Disconnect(); }

  bool Connect() override {
    if (connected_) {
      return true;
    }
    auto log_fail = [this](const char* step) {
      const char* msg = RVC::GetLastErrorMessage();
      std::fprintf(stderr, "[相机] 连接失败 id=%s sn=%s (%s) %s\n", id_.c_str(), serial_.c_str(),
                   step, msg ? msg : "");
      std::fflush(stderr);
    };

    if (!EnsureRvcSystemInit()) {
      log_fail("初始化");
      return false;
    }
    system_inited_ = true;

    device_ = RVC::SystemFindDevice(serial_.c_str());
    if (!device_.IsValid()) {
      RVC::Device listed[16];
      size_t actual = 0;
      RVC::SystemListDevices(listed, 16, &actual, RVC::SystemListDeviceType::All);
      std::fprintf(stderr, "[相机] 未找到序列号=%s，当前在线=%zu 台\n", serial_.c_str(), actual);
      std::fflush(stderr);
      log_fail("查找设备");
      ReleaseRvcSystemInit();
      system_inited_ = false;
      return false;
    }
    cam_ = RVC::X2::Create(device_);
    if (!cam_.has_value() || !cam_->IsValid() || !cam_->Open()) {
      log_fail(cam_.has_value() && cam_->IsValid() ? "打开" : "创建");
      if (cam_.has_value()) {
        RVC::X2::Destroy(*cam_);
        cam_.reset();
      }
      if (device_.IsValid()) {
        RVC::Device::Destroy(device_);
        device_ = RVC::Device{};
      }
      ReleaseRvcSystemInit();
      system_inited_ = false;
      return false;
    }
    connected_ = true;
    // 连接后缓存机内采集参数（含机型实际支持的 capture_mode），并强制软件触发
    {
      RVC::X2::CaptureOptions opts;
      if (cam_->LoadCaptureOptionParameters(opts)) {
        opts.trigger_mode = RVC::TriggerMode_SoftWare;
        recipe_opts_ = opts;
        has_recipe_opts_ = true;
      } else {
        RVC::X2::CaptureOptions fallback;
        fallback.capture_mode = PickSupportedCaptureMode();
        fallback.trigger_mode = RVC::TriggerMode_SoftWare;
        recipe_opts_ = fallback;
        has_recipe_opts_ = true;
      }
    }
    return true;
  }

  void Disconnect() override {
    if (cam_.has_value()) {
      if (cam_->IsValid()) {
        cam_->Close();
      }
      RVC::X2::Destroy(*cam_);
      cam_.reset();
    }
    if (device_.IsValid()) {
      RVC::Device::Destroy(device_);
      device_ = RVC::Device{};
    }
    if (system_inited_) {
      ReleaseRvcSystemInit();
      system_inited_ = false;
    }
    connected_ = false;
  }

  bool IsConnected() const override { return connected_; }

  CameraInfo GetInfo() const override {
    CameraInfo info;
    info.id = id_;
    info.serial = serial_;
    info.connected = connected_;
    info.is_stub = false;
    return info;
  }

  /** 采图失败后 Close/Open，避免 SDK 进入 “Device can not get status”。 */
  bool ReopenCamera() {
    if (!cam_.has_value()) {
      return false;
    }
    cam_->Close();
    if (!cam_->Open()) {
      connected_ = false;
      const char* msg = RVC::GetLastErrorMessage();
      std::fprintf(stderr, "[相机] 重新打开失败 id=%s sn=%s %s\n", id_.c_str(), serial_.c_str(),
                   msg ? msg : "");
      std::fflush(stderr);
      return false;
    }
    connected_ = true;
    return true;
  }

  /** 选一个设备实际支持的 CaptureMode（G51000 等机型常不支持 Normal/Fast）。 */
  RVC::CaptureMode PickSupportedCaptureMode() {
    RVC::DeviceInfo info{};
    unsigned supported = 0;
    RVC::Device& dev = device_;
    if (dev.IsValid() && dev.GetDeviceInfo(&info)) {
      supported = static_cast<unsigned>(info.support_capture_mode);
    }
    const RVC::CaptureMode prefer[] = {
        RVC::CaptureMode_Ultra,
        RVC::CaptureMode_AntiInterReflection,
        RVC::CaptureMode_SwingLineScan,
        RVC::CaptureMode_Normal,
        RVC::CaptureMode_Fast,
        RVC::CaptureMode_FixedLineScan,
        RVC::CaptureMode_LineArrayShift,
    };
    for (RVC::CaptureMode m : prefer) {
      if (supported == 0 || (supported & static_cast<unsigned>(m)) != 0) {
        return m;
      }
    }
    return RVC::CaptureMode_Ultra;
  }

  /** 组装软件触发用的 CaptureOptions（优先机内/配方参数）。 */
  RVC::X2::CaptureOptions BuildSoftCaptureOptions() {
    RVC::X2::CaptureOptions opts;
    if (has_recipe_opts_) {
      opts = recipe_opts_;
    } else if (cam_.has_value() && cam_->LoadCaptureOptionParameters(opts)) {
      // keep loaded mode/exposure
    } else {
      opts.capture_mode = PickSupportedCaptureMode();
    }
    opts.trigger_mode = RVC::TriggerMode_SoftWare;
    return opts;
  }

  bool TryCaptureOnce(const RVC::X2::CaptureOptions& opts, const char* step) {
    const bool ok = cam_->Capture(opts);
    if (!ok) {
      const char* msg = RVC::GetLastErrorMessage();
      std::fprintf(stderr, "[相机] 采图失败 id=%s sn=%s (%s) %s\n", id_.c_str(), serial_.c_str(),
                   step, msg ? msg : "");
      std::fflush(stderr);
    }
    return ok;
  }

  /** 实机：从 RVC SDK 拷贝 DepthMap/PointMap 到内存 buffer（float64 mm）。 */
  CaptureBundle Capture() override {
    CaptureBundle bundle;
    bundle.camera_serial = serial_;
    if (!connected_ || !cam_.has_value()) {
      bundle.error_message = "camera not connected";
      return bundle;
    }

    // 始终软件触发：PLC 边沿 → 本进程 Soft Capture。
    // 禁止无参 Capture()/默认 Normal：部分机型不支持 Normal，会 Collect Failed。
    RVC::X2::CaptureOptions opts = BuildSoftCaptureOptions();
    bool captured = TryCaptureOnce(opts, "primary");
    if (!captured) {
      if (!ReopenCamera()) {
        bundle.error_message = "Capture failed and reopen failed";
        return bundle;
      }
      // 回退：机内参数再读一次
      RVC::X2::CaptureOptions loaded;
      if (cam_->LoadCaptureOptionParameters(loaded)) {
        loaded.trigger_mode = RVC::TriggerMode_SoftWare;
        captured = TryCaptureOnce(loaded, "reopen+loaded");
        if (captured) {
          recipe_opts_ = loaded;
          has_recipe_opts_ = true;
          opts = loaded;
        }
      }
      if (!captured) {
        RVC::X2::CaptureOptions fallback;
        fallback.capture_mode = PickSupportedCaptureMode();
        fallback.trigger_mode = RVC::TriggerMode_SoftWare;
        captured = TryCaptureOnce(fallback, "reopen+supported-mode");
        if (captured) {
          recipe_opts_ = fallback;
          has_recipe_opts_ = true;
          opts = fallback;
        }
      }
      if (!captured) {
        const char* msg = RVC::GetLastErrorMessage();
        bundle.error_message =
            std::string("Capture failed: ") + (msg && msg[0] ? msg : "unknown RVC error");
        return bundle;
      }
    }

    RVC::DepthMap depth = cam_->GetDepthMap();
    RVC::PointMap point_map = cam_->GetPointMap();

    if (depth.IsValid()) {
      const RVC::Size sz = depth.GetSize();
      const double* src = depth.GetDataConstPtr();
      if (src != nullptr && sz.cols > 0 && sz.rows > 0) {
        auto depth_buf = std::make_shared<DepthImageBuffer>();
        depth_buf->width = static_cast<std::uint32_t>(sz.cols);
        depth_buf->height = static_cast<std::uint32_t>(sz.rows);
        depth_buf->format = DepthPixelFormat::kFloat64Mm;
        const std::size_t bytes =
            static_cast<std::size_t>(sz.cols) * sz.rows * sizeof(double);
        depth_buf->data.resize(bytes);
        std::memcpy(depth_buf->data.data(), src, bytes);
        bundle.depth = std::move(depth_buf);
      }
    }

    if (point_map.IsValid()) {
      const RVC::Size sz = point_map.GetSize();
      const double* src = point_map.GetPointDataConstPtr();
      if (src != nullptr && sz.cols > 0 && sz.rows > 0) {
        auto pc_buf = std::make_shared<PointCloudBuffer>();
        pc_buf->format = PointCloudFormat::kXyzFloat64Mm;
        pc_buf->point_count = static_cast<std::uint64_t>(sz.cols) * sz.rows;
        const std::size_t bytes = pc_buf->point_count * 3 * sizeof(double);
        pc_buf->data.resize(bytes);
        std::memcpy(pc_buf->data.data(), src, bytes);
        bundle.pointcloud = std::move(pc_buf);
      }
    }

    RVC::Image img = cam_->GetImage(RVC::CameraID_Left);
    if (img.IsValid()) {
      const RVC::Size sz = img.GetSize();
      const unsigned char* src = img.GetDataConstPtr();
      if (src != nullptr && sz.cols > 0 && sz.rows > 0) {
        auto gray = std::make_shared<GrayImageBuffer>();
        gray->width = static_cast<std::uint32_t>(sz.cols);
        gray->height = static_cast<std::uint32_t>(sz.rows);
        const std::size_t pixel_count = static_cast<std::size_t>(sz.cols) * sz.rows;
        gray->data.resize(pixel_count);
        const auto type = img.GetType();
        if (type == RVC::ImageType::Mono8) {
          std::memcpy(gray->data.data(), src, pixel_count);
        } else if (type == RVC::ImageType::RGB8) {
          for (std::size_t i = 0; i < pixel_count; ++i) {
            const unsigned char r = src[i * 3 + 0];
            const unsigned char g = src[i * 3 + 1];
            const unsigned char b = src[i * 3 + 2];
            gray->data[i] = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
          }
        } else if (type == RVC::ImageType::BGR8) {
          for (std::size_t i = 0; i < pixel_count; ++i) {
            const unsigned char b = src[i * 3 + 0];
            const unsigned char g = src[i * 3 + 1];
            const unsigned char r = src[i * 3 + 2];
            gray->data[i] = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
          }
        } else {
          gray.reset();
        }
        bundle.gray = std::move(gray);
      }
    }

    bundle.ok = bundle.depth != nullptr;
    if (!bundle.ok) {
      bundle.error_message = "Capture ok but DepthMap invalid/empty";
      std::fprintf(stderr, "[相机] 采图成功但深度为空 id=%s sn=%s\n", id_.c_str(), serial_.c_str());
      std::fflush(stderr);
    }
    return bundle;
  }

  bool SaveLastCaptureToDir(const std::string& session_dir, const std::string& prefix,
                            CaptureBundle* bundle) override {
    // 实机：RVC::DepthMap::SaveDepthMap → TIFF（须在下次 Capture 前调用）
    if (bundle == nullptr || !bundle->ok || !connected_ || !cam_.has_value()) {
      return false;
    }

    RVC::DepthMap depth = cam_->GetDepthMap();
    if (!depth.IsValid()) {
      return false;
    }

    // session_dir 来自 ResolveDataRoot()：Windows 下是本地窄字符（ACP），
    // 必须用 fs::path(session_dir)，不能 PathFromUtf8（会把 GBK 误当 UTF-8）。
    const fs::path out_dir(session_dir);
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    // 官方示例均为 .tiff
    const fs::path final_path = out_dir / (prefix + "_depth.tiff");
    bundle->rgb_path.clear();
#ifdef _WIN32
    {
      const std::wstring w = final_path.wstring();
      const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
      if (n > 1) {
        std::string utf8(static_cast<std::size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, utf8.data(), n, nullptr, nullptr);
        bundle->depth_path = std::move(utf8);
      } else {
        bundle->depth_path = final_path.string();
      }
    }
#else
    bundle->depth_path = final_path.string();
#endif

    // SaveDepthMap(const char*)：先写 ASCII 临时路径，再搬到目标目录；is_m=false → 毫米
#ifdef _WIN32
    char temp_dir_a[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, temp_dir_a) == 0) {
      return false;
    }
    const std::string temp_path =
        std::string(temp_dir_a) + "vs_rvc_depth_" + std::to_string(GetCurrentProcessId()) + ".tiff";
#else
    const std::string temp_path =
        (fs::temp_directory_path() / ("vs_rvc_depth_" + std::to_string(::getpid()) + ".tiff")).string();
#endif

    if (!depth.SaveDepthMap(temp_path.c_str(), false)) {
      return false;
    }

    const fs::path temp_fs(temp_path);
    std::error_code size_ec;
    if (!fs::exists(temp_fs) || fs::file_size(temp_fs, size_ec) == 0) {
      std::error_code rm_ec;
      fs::remove(temp_fs, rm_ec);
      return false;
    }

    ec.clear();
    fs::copy_file(temp_fs, final_path, fs::copy_options::overwrite_existing, ec);
    std::error_code rm_ec;
    fs::remove(temp_fs, rm_ec);
    return !ec && fs::exists(final_path);
  }

  CaptureBundle CaptureToDir(const std::string& session_dir, const std::string& prefix) override {
    CaptureBundle bundle = Capture();
    if (!bundle.ok) {
      return bundle;
    }
    if (!SaveLastCaptureToDir(session_dir, prefix, &bundle)) {
      bundle.ok = false;
    }
    return bundle;
  }

  bool LoadRecipeFile(const std::string& file_path, RecipeParamList* out_params = nullptr) override {
    if (!connected_ || !cam_.has_value()) {
      return false;
    }
    if (!fs::exists(PathFromUtf8(file_path))) {
      return false;
    }

    // RVC LoadSettingFromFile(const char*) 对中文路径不稳定：先拷到 ASCII 临时文件再加载。
    std::string load_path = file_path;
    bool used_temp = false;
#ifdef _WIN32
    {
      char temp_dir[MAX_PATH]{};
      GetTempPathA(MAX_PATH, temp_dir);
      load_path = std::string(temp_dir) + "vs_rvc_recipe_" +
                  std::to_string(GetCurrentProcessId()) + ".json";
      std::error_code copy_ec;
      fs::copy_file(PathFromUtf8(file_path), PathFromUtf8(load_path),
                    fs::copy_options::overwrite_existing, copy_ec);
      if (copy_ec) {
        return false;
      }
      used_temp = true;
    }
#endif

    const bool ok = cam_->LoadSettingFromFile(load_path.c_str());
    if (used_temp) {
      std::error_code rm_ec;
      fs::remove(PathFromUtf8(load_path), rm_ec);
    }
    if (!ok) {
      return false;
    }

    RVC::X2::CaptureOptions opts;
    if (cam_->LoadCaptureOptionParameters(opts)) {
      opts.trigger_mode = RVC::TriggerMode_SoftWare;
      recipe_opts_ = opts;
      has_recipe_opts_ = true;
      if (out_params != nullptr) {
        FillRecipeParamsFromCaptureOptions(opts, out_params);
      }
    } else if (out_params != nullptr) {
      FillRecipeParamsFromJsonFile(file_path, out_params);
    }
    return true;
  }

 private:
  std::string id_;
  std::string serial_;
  RVC::Device device_{};
  std::optional<RVC::X2> cam_;
  bool connected_ = false;
  bool system_inited_ = false;
  bool has_recipe_opts_ = false;
  RVC::X2::CaptureOptions recipe_opts_{};
};
#endif

}  // namespace

CameraPtr CreateRvcCamera(const std::string& id, const std::string& serial, bool force_stub,
                          const StubCameraOptions& stub_options) {
  // 序列号以 STUB_ 开头：即使 production 也用桩机，避免假序列号导致整盘“相机异常”
  const bool serial_is_stub =
      serial.size() >= 5 && (serial.compare(0, 5, "STUB_") == 0 || serial.compare(0, 5, "stub_") == 0);
  if (force_stub || serial_is_stub || serial.empty()) {
    return std::make_shared<StubRvcCamera>(id, serial.empty() ? "STUB" : serial, stub_options);
  }
#ifdef VISUAL_HAS_RVC_SDK
  return std::make_shared<RvcCamera>(id, serial);
#else
  StubCameraOptions legacy_options;
  legacy_options.image_width = 128;
  legacy_options.image_height = 96;
  legacy_options.solid_black = false;
  return std::make_shared<StubRvcCamera>(id, serial, legacy_options);
#endif
}

}  // namespace visual

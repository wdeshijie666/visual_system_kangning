/**
 * @file rvc_camera_adapter.cpp
 * @brief RVC 相机适配：Capture() 内存采集（阶段 6.2），仿真 Stub / 实机 RVC SDK。
 * 详见 docs/框架流程通路.md §4.1、§6.2
 */
#include "visual/rvc_camera_adapter.h"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "visual/log_format.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef VISUAL_STUB_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
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

/** SmartGuide.exe 所在目录（仿真 TIFF 固定相对此目录）。 */
fs::path ResolveHostExeDir() {
#ifdef _WIN32
  char buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return fs::path(buf).parent_path();
  }
#endif
  return fs::current_path();
}

/**
 * 仿真：从 TIFF 加载深度。
 * SHM 浮点深度约定为「米」（后续 ConvertShmDepthMetersToMm 会 ×1000）；
 * 若 TIFF 数值像毫米（max>20）则先换成米再写入。
 */
bool LoadStubDepthFromTiff(const fs::path& tiff_path, std::shared_ptr<DepthImageBuffer>* out,
                           std::string* error) {
  if (out == nullptr) {
    return false;
  }
#ifndef VISUAL_STUB_HAS_OPENCV
  if (error) {
    *error = "未链接 OpenCV，无法读取 sim_test.tiff";
  }
  return false;
#else
  if (!fs::exists(tiff_path)) {
    if (error) {
      *error = "仿真深度文件不存在: " + tiff_path.string();
    }
    return false;
  }
  cv::Mat img = cv::imread(tiff_path.string(), cv::IMREAD_UNCHANGED);
  if (img.empty() || img.channels() != 1) {
    if (error) {
      *error = "仿真深度 TIFF 无效（需单通道）: " + tiff_path.string();
    }
    return false;
  }
  cv::Mat f32;
  if (img.type() == CV_32FC1) {
    f32 = img;
  } else if (img.type() == CV_64FC1) {
    img.convertTo(f32, CV_32FC1);
  } else if (img.type() == CV_16UC1) {
    img.convertTo(f32, CV_32FC1);
  } else {
    img.convertTo(f32, CV_32FC1);
  }
  double min_v = 0.0;
  double max_v = 0.0;
  cv::minMaxLoc(f32, &min_v, &max_v);
  // 毫米量级 → 米（与在线相机浮点深度一致）
  if (max_v > 20.0) {
    f32 *= (1.f / 1000.f);
  }
  if (!f32.isContinuous()) {
    f32 = f32.clone();
  }
  auto depth = std::make_shared<DepthImageBuffer>();
  depth->width = static_cast<std::uint32_t>(f32.cols);
  depth->height = static_cast<std::uint32_t>(f32.rows);
  depth->format = DepthPixelFormat::kFloat32Mm;
  const std::size_t nbytes = f32.total() * f32.elemSize();
  depth->data.resize(nbytes);
  std::memcpy(depth->data.data(), f32.data, nbytes);
  *out = std::move(depth);
  return true;
#endif
}

class StubRvcCamera final : public ICamera3D {
 public:
  StubRvcCamera(std::string id, std::string serial, StubCameraOptions options)
      : id_(std::move(id)), serial_(std::move(serial)), options_(options) {}

  bool Connect() override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    connected_ = true;
    return true;
  }
  void Disconnect() override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    connected_ = false;
  }
  bool IsConnected() const override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    return connected_;
  }

  CameraProbeResult ProbeAlive() override {
    std::unique_lock<std::recursive_mutex> lock(api_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return CameraProbeResult::kBusy;
    }
    return connected_ ? CameraProbeResult::kAlive : CameraProbeResult::kDead;
  }

  CameraInfo GetInfo() const override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    CameraInfo info;
    info.id = id_;
    info.serial = serial_;
    info.ip = "stub";
    info.connected = connected_;
    info.is_stub = true;
    return info;
  }

  /** 仿真：优先读 exe 旁 sim_test.tiff；失败则报错（不静默用假深度，避免压测失真）。 */
  CaptureBundle Capture(const CaptureCopyOptions& opts = {}) override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    const auto t0 = std::chrono::steady_clock::now();
    CaptureBundle bundle;
    bundle.camera_serial = serial_;
    bundle.ok = true;

    std::shared_ptr<DepthImageBuffer> depth_from_tiff;
    if (opts.copy_depth && !options_.sim_depth_tiff.empty()) {
      const fs::path tiff = ResolveHostExeDir() / options_.sim_depth_tiff;
      std::string load_err;
      if (!LoadStubDepthFromTiff(tiff, &depth_from_tiff, &load_err)) {
        bundle.ok = false;
        bundle.error_message = load_err.empty() ? "仿真深度 TIFF 读取失败" : load_err;
        LogToStderr(LogSeverity::kWarning, "[stub] " + bundle.error_message);
      }
    }

    int w = options_.image_width;
    int h = options_.image_height;
    if (depth_from_tiff) {
      w = static_cast<int>(depth_from_tiff->width);
      h = static_cast<int>(depth_from_tiff->height);
    }

    if (opts.copy_depth) {
      if (depth_from_tiff) {
        bundle.depth = depth_from_tiff;
      } else if (bundle.ok) {
        bundle.depth = MakeStubDepthBuffer(w, h, options_.solid_black);
      }
    }
    if (opts.copy_gray && bundle.ok) {
      bundle.gray = MakeStubGrayBuffer(w, h, options_.solid_black);
    }
    if (opts.copy_pointcloud && bundle.ok) {
      bundle.pointcloud = MakeStubPointCloudBuffer();
    }
    if (opts.copy_depth && bundle.ok && !bundle.depth) {
      bundle.ok = false;
      bundle.error_message = "stub depth alloc failed";
    }
    bundle.capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    return bundle;
  }

  bool SaveLastCaptureToDir(const std::string& session_dir, const std::string& prefix,
                            CaptureBundle* bundle) override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
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
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
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
  mutable std::recursive_mutex api_mutex_;
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

  /** 释放 SDK 句柄（调用方已持有 api_mutex_）。 */
  void ReleaseSdkUnlocked() {
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

  bool Connect() override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    if (connected_ && cam_.has_value() && cam_->IsValid() && cam_->IsOpen() &&
        cam_->IsPhysicallyConnected()) {
      return true;
    }
    // 半开/假在线：先清句柄再连，避免泄漏或重复 Create
    ReleaseSdkUnlocked();

    auto log_fail = [this](const char* step) {
      const char* msg = RVC::GetLastErrorMessage();
      std::ostringstream oss;
      oss << "相机连接失败 id=" << id_ << " sn=" << serial_ << " (" << step << ") "
          << (msg ? msg : "");
      LogToStderr(LogSeverity::kWarning, oss.str());
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
      std::ostringstream oss;
      oss << "相机未找到序列号=" << serial_ << "，当前在线=" << actual << " 台";
      LogToStderr(LogSeverity::kWarning, oss.str());
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
    // 采图使用机内当前参数（无参 Capture），连接后不组装/下发 CaptureOptions
    return true;
  }

  void Disconnect() override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    ReleaseSdkUnlocked();
  }

  bool IsConnected() const override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    return connected_;
  }

  CameraProbeResult ProbeAlive() override {
    std::unique_lock<std::recursive_mutex> lock(api_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return CameraProbeResult::kBusy;
    }
    if (!connected_ || !cam_.has_value()) {
      return CameraProbeResult::kDead;
    }
    // 轻量查询：不 Capture、不写参
    if (!cam_->IsValid() || !cam_->IsOpen() || !cam_->IsPhysicallyConnected()) {
      connected_ = false;
      return CameraProbeResult::kDead;
    }
    return CameraProbeResult::kAlive;
  }

  CameraInfo GetInfo() const override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
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
      std::ostringstream oss;
      oss << "相机重新打开失败 id=" << id_ << " sn=" << serial_ << " " << (msg ? msg : "");
      LogToStderr(LogSeverity::kWarning, oss.str());
      return false;
    }
    connected_ = true;
    return true;
  }

  /** 无参 Capture：SDK 从机内加载当前参数，不会把本进程组装的 opts 写回相机。 */
  bool TryCaptureOnce(const char* step, std::int64_t* out_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = cam_->Capture();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    if (out_ms != nullptr) {
      *out_ms = ms;
    }
    if (!ok) {
      const char* msg = RVC::GetLastErrorMessage();
      std::ostringstream oss;
      oss << "相机采图失败 id=" << id_ << " sn=" << serial_ << " (" << step << ") 耗时=" << ms
          << "ms " << (msg ? msg : "");
      LogToStderr(LogSeverity::kWarning, oss.str());
    } else {
      std::ostringstream oss;
      oss << "相机采图完成 id=" << id_ << " sn=" << serial_ << " (" << step << ") 耗时=" << ms
          << "ms";
      LogToStderr(LogSeverity::kInfo, oss.str());
    }
    return ok;
  }

  /** 实机：按 opts 决定是否拷贝 Depth/PointMap/灰度到堆。 */
  CaptureBundle Capture(const CaptureCopyOptions& opts = {}) override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
    CaptureBundle bundle;
    bundle.camera_serial = serial_;
    if (!connected_ || !cam_.has_value()) {
      bundle.error_message = "camera not connected";
      return bundle;
    }

    std::int64_t capture_ms = -1;
    bool captured = TryCaptureOnce("primary", &capture_ms);
    if (!captured) {
      if (!ReopenCamera()) {
        bundle.error_message = "Capture failed and reopen failed";
        bundle.capture_ms = capture_ms;
        return bundle;
      }
      captured = TryCaptureOnce("reopen", &capture_ms);
      if (!captured) {
        const char* msg = RVC::GetLastErrorMessage();
        bundle.error_message =
            std::string("Capture failed: ") + (msg && msg[0] ? msg : "unknown RVC error");
        bundle.capture_ms = capture_ms;
        return bundle;
      }
    }
    bundle.capture_ms = capture_ms;

    if (opts.copy_depth) {
      RVC::DepthMap depth = cam_->GetDepthMap();
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
    }

    if (opts.copy_pointcloud) {
      RVC::PointMap point_map = cam_->GetPointMap();
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
    }

    if (opts.copy_gray) {
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
    }

    // SDK 采图成功即 ok；深度堆拷贝按需。落盘仍可用机内 DepthMap。
    bundle.ok = true;
    if (opts.copy_depth && !bundle.depth) {
      bundle.ok = false;
      bundle.error_message = "Capture ok but DepthMap invalid/empty";
      std::ostringstream oss;
      oss << "相机采图成功但深度为空 id=" << id_ << " sn=" << serial_;
      LogToStderr(LogSeverity::kWarning, oss.str());
    }
    return bundle;
  }

  bool SaveLastCaptureToDir(const std::string& session_dir, const std::string& prefix,
                            CaptureBundle* bundle) override {
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
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
    std::lock_guard<std::recursive_mutex> lock(api_mutex_);
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

    // 仅用于 UI 展示；采图仍走无参 Capture()，使用机内（含本配方）当前参数
    if (out_params != nullptr) {
      RVC::X2::CaptureOptions opts;
      if (cam_->LoadCaptureOptionParameters(opts)) {
        FillRecipeParamsFromCaptureOptions(opts, out_params);
      } else {
        FillRecipeParamsFromJsonFile(file_path, out_params);
      }
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
  /** 串行化 Connect/Capture/Disconnect/配方加载，避免巡检与周期采图并发踩 SDK。 */
  mutable std::recursive_mutex api_mutex_;
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

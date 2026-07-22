/**
 * @file data_recorder.cpp
 * @brief 数据存根落盘实现。流程见 docs/框架流程通路.md §6.1、§6.5、§7。
 */
#include "visual/data_recorder.h"

#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "visual/app_context.h"
#include "visual/event_bus.h"

namespace fs = std::filesystem;

namespace visual {
namespace {

constexpr int kTimestampPrefixLength = 15;  // yyyyMMdd_hhmmss

#ifdef _WIN32
std::ofstream OpenBinaryFile(const fs::path& path) {
  return std::ofstream(path.wstring().c_str(), std::ios::binary);
}
#else
std::ofstream OpenBinaryFile(const fs::path& path) {
  return std::ofstream(path, std::ios::binary);
}
#endif

std::string StationTag(StationId station) {
  switch (station) {
    case StationId::kR09:
      return "R09";
    case StationId::kR07:
      return "R07";
    case StationId::kR05:
    default:
      return "R05";
  }
}

bool WriteTimestampStrings(std::string* date_folder, std::string* timestamp_prefix) {
  if (date_folder == nullptr || timestamp_prefix == nullptr) {
    return false;
  }
  const auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream date_oss;
  date_oss << std::put_time(&tm, "%Y%m%d");
  *date_folder = date_oss.str();

  std::ostringstream prefix_oss;
  prefix_oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
  *timestamp_prefix = prefix_oss.str();
  return true;
}

bool IsDigitTimestampPrefix(const std::string& prefix) {
  if (prefix.size() != kTimestampPrefixLength || prefix[8] != '_') {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (i == 8) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<std::chrono::system_clock::time_point> ParseStubTimestamp(const std::string& filename) {
  if (filename.size() < kTimestampPrefixLength) {
    return std::nullopt;
  }
  const std::string prefix = filename.substr(0, kTimestampPrefixLength);
  if (!IsDigitTimestampPrefix(prefix)) {
    return std::nullopt;
  }

  std::tm tm{};
  std::istringstream iss(prefix);
  iss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
  if (iss.fail()) {
    return std::nullopt;
  }
  std::time_t t = std::mktime(&tm);
  if (t == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(t);
}

bool WriteBlackGrayscalePgm(const fs::path& path, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  std::ofstream out = OpenBinaryFile(path);
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

bool WriteUint16DepthPgm(const fs::path& path, const DepthImageBuffer& depth) {
  if (depth.format != DepthPixelFormat::kUint16Mm || depth.width == 0 || depth.height == 0) {
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(depth.width) * depth.height * 2;
  if (depth.data.size() < expected) {
    return false;
  }
  std::ofstream out = OpenBinaryFile(path);
  if (!out) {
    return false;
  }
  out << "P5\n" << depth.width << " " << depth.height << "\n65535\n";
  out.write(reinterpret_cast<const char*>(depth.data.data()), static_cast<std::streamsize>(expected));
  return out.good();
}

bool WriteFloat64DepthRaw(const fs::path& path, const DepthImageBuffer& depth) {
  if (depth.format != DepthPixelFormat::kFloat64Mm || depth.width == 0 || depth.height == 0) {
    return false;
  }
  const std::size_t expected =
      static_cast<std::size_t>(depth.width) * depth.height * sizeof(double);
  if (depth.data.size() < expected) {
    return false;
  }
  std::ofstream out = OpenBinaryFile(path);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(&depth.width), sizeof(depth.width));
  out.write(reinterpret_cast<const char*>(&depth.height), sizeof(depth.height));
  out.write(reinterpret_cast<const char*>(depth.data.data()), static_cast<std::streamsize>(expected));
  return out.good();
}

bool WriteBlackGrayscaleBmp(const fs::path& path, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }

  const int row_stride = ((width + 3) / 4) * 4;
  const std::uint32_t pixel_data_size = static_cast<std::uint32_t>(row_stride * height);
  const std::uint32_t file_size = 14 + 40 + 256 * 4 + pixel_data_size;

  std::ofstream out = OpenBinaryFile(path);
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

bool WritePointCloudPly(const fs::path& path, const PointCloudBuffer& pc) {
  if (pc.format == PointCloudFormat::kXyzFloat64Mm && pc.point_count > 0) {
    const std::size_t expected = static_cast<std::size_t>(pc.point_count) * 3 * sizeof(double);
    if (pc.data.size() < expected) {
      return false;
    }
    std::ofstream out = OpenBinaryFile(path);
    if (!out) {
      return false;
    }
    // 二进制 PLY：全分辨率点云用 ASCII 会极慢，表现为“未保存”
    out << "ply\nformat binary_little_endian 1.0\nelement vertex " << pc.point_count
        << "\nproperty double x\nproperty double y\nproperty double z\nend_header\n";
    out.write(reinterpret_cast<const char*>(pc.data.data()), static_cast<std::streamsize>(expected));
    return out.good();
  }

  std::ofstream out = OpenBinaryFile(path);
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

}  // namespace

std::string ResolveDataRoot(const std::string& configured_path) {
  if (configured_path.empty()) {
    return "./data";
  }
  fs::path path = fs::u8path(configured_path);
  if (path.is_absolute()) {
    return path.lexically_normal().string();
  }
  // 主程序启动已 QDir::setCurrent(exeDir)。周期线程/存盘线程禁止再调 QCoreApplication。
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  if (ec) {
    return path.lexically_normal().string();
  }
  return (cwd / path).lexically_normal().string();
}

CaptureRecordContext BuildCaptureRecordContext(StationId station, const std::string& configured_data_path) {
  // 在线：{dataRoot}/{yyyyMMdd}/ + 时间戳前缀 yyyyMMdd_hhmmss
  CaptureRecordContext ctx;
  ctx.station_tag = StationTag(station);

  std::string date_folder;
  if (!WriteTimestampStrings(&date_folder, &ctx.timestamp_prefix)) {
    return ctx;
  }

  const std::string root = ResolveDataRoot(configured_data_path);
  ctx.output_dir = (fs::path(root) / date_folder).lexically_normal().string();
  fs::create_directories(ctx.output_dir);
  return ctx;
}

std::string MakeCaptureFilePrefix(const CaptureRecordContext& ctx, const std::string& camera_id) {
  return ctx.timestamp_prefix + "_" + ctx.station_tag + "_" + camera_id;
}

void AssignCapturePaths(CaptureBundle* bundle, const std::string& session_dir, const std::string& prefix) {
  // 按 dataStub.saveDepth / savePointcloud 预生成路径；灰度图不落盘
  if (bundle == nullptr) {
    return;
  }
  std::error_code ec;
  fs::create_directories(fs::path(session_dir), ec);
  bundle->rgb_path.clear();
  const auto& settings = AppContext::Instance().Settings();
  if (settings.stub_save_depth) {
    // 实机 float64 → RVC SaveDepthMap 写 TIFF；仿真 uint16 → 自研 pgm
    const bool rvc_tiff = bundle->depth && bundle->depth->format == DepthPixelFormat::kFloat64Mm;
    const char* ext = rvc_tiff ? "_depth.tiff" : "_depth.pgm";
    bundle->depth_path = (fs::path(session_dir) / (prefix + ext)).string();
  } else {
    bundle->depth_path.clear();
  }
  if (settings.stub_save_pointcloud) {
    bundle->pointcloud_path = (fs::path(session_dir) / (prefix + "_pointcloud.ply")).string();
  } else {
    bundle->pointcloud_path.clear();
  }
}

bool SaveCaptureBundleToDir(const CaptureBundle& bundle) {
  if (!bundle.ok) {
    return false;
  }
  const bool want_depth = !bundle.depth_path.empty();
  const bool want_ply = !bundle.pointcloud_path.empty();
  if (!want_depth && !want_ply) {
    return true;  // 配置为全部不存根，视为成功
  }

  std::string session_dir;
  if (want_depth) {
    session_dir = fs::path(bundle.depth_path).parent_path().string();
  } else {
    session_dir = fs::path(bundle.pointcloud_path).parent_path().string();
  }

  {
    std::error_code space_ec;
    const auto info = fs::space(session_dir.empty() ? "." : session_dir, space_ec);
    if (!space_ec && info.available < 50ull * 1024ull * 1024ull) {
      EventBus::Instance().NotifyLog(QStringLiteral("disk low, skip stub save"));
      return false;
    }
  }

  std::error_code ec;
  fs::create_directories(fs::path(session_dir), ec);

  bool ok = true;
  if (want_depth) {
    bool depth_ok = false;
    const fs::path depth_fs = fs::path(bundle.depth_path);
    const std::string ext = depth_fs.extension().string();
    // 实机 TIFF 必须由 RVC::DepthMap::SaveDepthMap 写出（见 SaveLastCaptureToDir），此处不再二次写
    if (ext == ".tif" || ext == ".tiff" || ext == ".TIF" || ext == ".TIFF") {
      depth_ok = fs::exists(depth_fs);
      if (!depth_ok) {
        EventBus::Instance().NotifyLog(
            QString("save depth skipped/missing tiff (use camera SaveLastCaptureToDir): %1")
                .arg(QString::fromStdString(bundle.depth_path)));
      }
    } else if (!bundle.depth) {
      depth_ok = false;
    } else if (bundle.depth->format == DepthPixelFormat::kUint16Mm) {
      depth_ok = WriteUint16DepthPgm(depth_fs, *bundle.depth);
    } else if (bundle.depth->format == DepthPixelFormat::kFloat64Mm) {
      depth_ok = WriteFloat64DepthRaw(depth_fs, *bundle.depth);
    } else {
      depth_ok = WriteBlackGrayscalePgm(depth_fs, static_cast<int>(bundle.depth->width),
                                        static_cast<int>(bundle.depth->height));
    }
    if (!depth_ok && ext != ".tif" && ext != ".tiff" && ext != ".TIF" && ext != ".TIFF") {
      EventBus::Instance().NotifyLog(
          QString("save depth failed: %1").arg(QString::fromStdString(bundle.depth_path)));
    }
    ok = depth_ok && ok;
  }
  if (want_ply) {
    bool ply_ok = false;
    if (bundle.pointcloud) {
      ply_ok = WritePointCloudPly(fs::path(bundle.pointcloud_path), *bundle.pointcloud);
    } else {
      PointCloudBuffer stub;
      stub.format = PointCloudFormat::kXyzFloat64Mm;
      stub.point_count = 4;
      ply_ok = WritePointCloudPly(fs::path(bundle.pointcloud_path), stub);
    }
    if (!ply_ok) {
      EventBus::Instance().NotifyLog(
          QString("save ply failed: %1").arg(QString::fromStdString(bundle.pointcloud_path)));
    }
    ok = ply_ok && ok;
  }
  return ok;
}

CaptureSaveWorker& CaptureSaveWorker::Instance() {
  static CaptureSaveWorker worker;
  return worker;
}

CaptureSaveWorker::~CaptureSaveWorker() {
  Stop();
}

void CaptureSaveWorker::Start() {
  if (running_.exchange(true)) {
    return;
  }
  worker_ = std::thread([this]() { WorkerLoop(); });
}

void CaptureSaveWorker::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void CaptureSaveWorker::Enqueue(CaptureBundle bundle) {
  Start();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(bundle));
  }
  cv_.notify_one();
}

void CaptureSaveWorker::WorkerLoop() {
  // 后台线程：按 dataStub 开关写 depth / ply
  while (running_.load()) {
    CaptureBundle job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return !running_.load() || !queue_.empty(); });
      if (!running_.load() && queue_.empty()) {
        break;
      }
      if (queue_.empty()) {
        continue;
      }
      job = std::move(queue_.front());
      queue_.pop_front();
    }

    if (!SaveCaptureBundleToDir(job)) {
      EventBus::Instance().NotifyLog(
          QString("async capture save failed: %1").arg(QString::fromStdString(job.camera_serial)));
    }
  }
}

bool SaveAlgoResultCsv(const CaptureRecordContext& ctx, const LogResultBatch& logs,
                       const std::string& result_suffix) {
  if (ctx.output_dir.empty() || ctx.timestamp_prefix.empty()) {
    return false;
  }

  const fs::path file_path = fs::path(ctx.output_dir) /
                               (ctx.timestamp_prefix + "_" + ctx.station_tag + "_" + result_suffix + ".txt");

  std::ostringstream line;
  for (std::size_t i = 0; i < logs.size(); ++i) {
    if (i > 0) {
      line << ',';
    }
    const auto& log = logs[i];
    line << static_cast<int>(log.status) << ','
         << log.offset_x_mm << ','
         << log.offset_y_mm << ','
         << log.offset_r_deg << ','
         << log.diameter_mm << ','
         << log.length_mm;
  }

  std::ofstream out = OpenBinaryFile(file_path);
  if (!out.is_open()) {
    return false;
  }
  out << line.str() << '\n';
  return out.good();
}

bool SessionDirHasCaptureData(const std::string& session_dir) {
  if (session_dir.empty() || !fs::exists(session_dir)) {
    return false;
  }
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(session_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.find("_depth.") != std::string::npos || name.find("_pointcloud.") != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string FindDepthImageInSession(const std::string& session_dir, const std::string& station_tag,
                                    const std::string& camera_id) {
  // 兼容旧名：优先找点云，其次才找历史深度图
  if (session_dir.empty() || !fs::exists(session_dir)) {
    return {};
  }

  auto find_by_token = [&](const char* token) -> std::string {
    std::string fallback;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(session_dir, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      const std::string name = entry.path().filename().string();
      if (name.find(token) == std::string::npos) {
        continue;
      }
      if (!station_tag.empty() && name.find("_" + station_tag + "_") == std::string::npos) {
        continue;
      }
      if (!camera_id.empty() && name.find("_" + camera_id + "_") != std::string::npos) {
        return entry.path().string();
      }
      if (fallback.empty()) {
        fallback = entry.path().string();
      }
    }
    return fallback;
  };

  const std::string depth = find_by_token("_depth.");
  if (!depth.empty()) {
    return depth;
  }
  return find_by_token("_pointcloud.");
}

CaptureRecordContext BuildReplayRecordContext(const std::string& session_dir, StationId station) {
  CaptureRecordContext ctx;
  ctx.output_dir = fs::path(session_dir).lexically_normal().string();
  ctx.station_tag = StationTag(station);

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(session_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.size() < kTimestampPrefixLength) {
      continue;
    }
    const std::string prefix = name.substr(0, kTimestampPrefixLength);
    if (!IsDigitTimestampPrefix(prefix)) {
      continue;
    }
    if (!ctx.station_tag.empty() && name.find("_" + ctx.station_tag + "_") == std::string::npos) {
      continue;
    }
    ctx.timestamp_prefix = prefix;
    break;
  }

  if (ctx.timestamp_prefix.empty()) {
    ctx.timestamp_prefix = fs::path(session_dir).filename().string();
  }
  return ctx;
}

DataStubRetentionCleaner::DataStubRetentionCleaner() = default;

DataStubRetentionCleaner::~DataStubRetentionCleaner() {
  Stop();
}

void DataStubRetentionCleaner::Start(const std::string& configured_data_path, int retention_days,
                                     int poll_interval_sec) {
  if (running_.exchange(true)) {
    return;
  }
  data_path_ = configured_data_path;
  retention_days_ = retention_days > 0 ? retention_days : 7;
  poll_interval_sec_ = poll_interval_sec > 0 ? poll_interval_sec : 3600;
  worker_ = std::thread([this]() { WorkerLoop(); });
}

void DataStubRetentionCleaner::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void DataStubRetentionCleaner::WorkerLoop() {
  while (running_.load()) {
    const std::size_t removed = PurgeExpiredStubsOnce(data_path_, retention_days_);
    if (removed > 0) {
      EventBus::Instance().NotifyLog(
          QString("data stub cleanup removed %1 file(s), keep %2 day(s)")
              .arg(static_cast<qulonglong>(removed))
              .arg(retention_days_));
    }

    for (int elapsed = 0; running_.load() && elapsed < poll_interval_sec_; elapsed += 1) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

std::size_t DataStubRetentionCleaner::PurgeExpiredStubsOnce(const std::string& configured_data_path,
                                                          int retention_days) const {
  const std::string root = ResolveDataRoot(configured_data_path);
  if (root.empty() || !fs::exists(root)) {
    return 0;
  }

  const auto cutoff =
      std::chrono::system_clock::now() - std::chrono::hours(24 * retention_days);
  std::size_t removed = 0;

  std::error_code ec;
  for (const auto& date_entry : fs::directory_iterator(root, ec)) {
    if (ec || !date_entry.is_directory()) {
      continue;
    }

    for (const auto& file_entry : fs::directory_iterator(date_entry.path(), ec)) {
      if (ec || !file_entry.is_regular_file()) {
        continue;
      }
      const auto timestamp = ParseStubTimestamp(file_entry.path().filename().string());
      if (!timestamp.has_value() || *timestamp >= cutoff) {
        continue;
      }
      std::error_code remove_ec;
      if (fs::remove(file_entry.path(), remove_ec) && !remove_ec) {
        ++removed;
      }
    }

    std::error_code empty_ec;
    if (fs::is_empty(date_entry.path(), empty_ec) && !empty_ec) {
      fs::remove(date_entry.path(), empty_ec);
    }
  }
  return removed;
}

}  // namespace visual

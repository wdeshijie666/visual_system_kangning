/**
 * @file algo_pointcloud_runner.cpp
 * @brief 点云算法：SHM / 离线 session 深度 / 临时 TIFF → 按工位配置创建的 PointCloudProcessor → 5 条结果。
 *
 * SHM 深度单位为米，固定换算为毫米后再计算；磁盘 TIFF/PGM（含历史回放）已是毫米。
 * PointCloudProcessor 配置文件与 topN 取自 algo_config.json 的 channels.r05/r09。
 */
#include "algo_pointcloud_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "algo_input_converter.h"
#include "algo_log.h"
#include "visual/algo_shm_codec.h"
#include "visual/capture_data_format.h"

namespace algo {
namespace {

/**
 * 从磁盘读深度图（TIFF/PGM 等，OpenCV 可读格式）。
 * 落盘约定为毫米，调用方勿再按 SHM「米→毫米」换算。
 */
bool LoadDepthTiffFile(const std::filesystem::path& path, cv::Mat* out, std::string* error) {
  if (out == nullptr) {
    if (error) {
      *error = "深度图输出为空";
    }
    return false;
  }
  if (!std::filesystem::exists(path)) {
    if (error) {
      *error = "深度图不存在: " + path.string();
    }
    return false;
  }
  cv::Mat img = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
  if (img.empty()) {
    if (error) {
      *error = "深度图读取失败: " + path.string();
    }
    return false;
  }
  if (img.channels() != 1) {
    if (error) {
      *error = "深度图须为单通道";
    }
    return false;
  }
  if (img.type() == CV_16UC1 || img.type() == CV_64FC1) {
    img.convertTo(*out, CV_32FC1);
  } else if (img.type() == CV_32FC1) {
    *out = img;
  } else {
    if (error) {
      *error = "深度图格式不支持";
    }
    return false;
  }
  return true;
}

/** 会话目录灰度（通常为 Mono8 PGM）；失败返回 false，不写 error。 */
bool LoadGrayImageFile(const std::filesystem::path& path, cv::Mat* out) {
  if (out == nullptr || path.empty() || !std::filesystem::exists(path)) {
    return false;
  }
  cv::Mat img = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
  if (img.empty()) {
    return false;
  }
  if (img.type() == CV_8UC1) {
    *out = img;
    return true;
  }
  if (img.channels() == 1) {
    img.convertTo(*out, CV_8UC1);
    return !out->empty();
  }
  return false;
}

std::string FirstCameraIdFromHeader(const visual::shm::ShmHeader* header) {
  if (header == nullptr) {
    return {};
  }
  for (std::int32_t i = 0; i < header->camera_count; ++i) {
    if (header->cameras[i].camera_serial[0] != '\0') {
      return header->cameras[i].camera_serial;
    }
  }
  return {};
}

/** SHM：米 → 毫米。uint16 仿真数据已是毫米。 */
void ConvertShmDepthMetersToMm(cv::Mat* depth) {
  if (depth == nullptr || depth->empty()) {
    return;
  }
  if (depth->type() == CV_64FC1) {
    depth->convertTo(*depth, CV_32FC1, 1000.0);
  } else if (depth->type() == CV_32FC1) {
    (*depth) *= 1000.f;
  } else if (depth->type() == CV_16UC1) {
    depth->convertTo(*depth, CV_32FC1);
  }
}

/** 非法深度（NaN/Inf/负值）置 0。 */
void SanitizeDepthMm(cv::Mat* depth) {
  if (depth == nullptr || depth->empty() || depth->type() != CV_32FC1) {
    return;
  }
  float* p = depth->ptr<float>();
  const std::size_t n = depth->total();
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isfinite(p[i]) || p[i] < 0.f) {
      p[i] = 0.f;
    }
  }
}

/** 正深度像素数：空点云直接跳过，不创建/触碰引擎。 */
std::size_t CountPositiveDepthMm(const cv::Mat& depth) {
  if (depth.empty() || depth.type() != CV_32FC1) {
    return 0;
  }
  const float* p = depth.ptr<float>();
  const std::size_t n = depth.total();
  std::size_t positive = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (std::isfinite(p[i]) && p[i] > 0.f) {
      ++positive;
    }
  }
  return positive;
}

void FillAllNg(visual::shm::ShmLogResult* logs, std::size_t count) {
  if (logs == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < count; ++i) {
    logs[i] = {};
    logs[i].status = 2;
  }
}

void MapFitResultsToLogs(const std::vector<PCP_FitResult>& fits, visual::shm::ShmLogResult* logs,
                         std::size_t count) {
  FillAllNg(logs, count);
  std::size_t auto_slot = 0;
  for (const auto& f : fits) {
    if (!f.valid) {
      continue;
    }
    std::size_t idx = 0;
    if (f.log_index >= 1) {
      idx = static_cast<std::size_t>(f.log_index - 1);
    } else {
      idx = auto_slot++;
    }
    if (idx >= count) {
      continue;
    }
    logs[idx].status = 1;
    logs[idx].offset_x_mm = f.offset_x;
    logs[idx].offset_y_mm = f.offset_y;
    logs[idx].offset_r_deg = f.tilt_deg;
    logs[idx].diameter_mm = f.diameter;
    logs[idx].length_mm = f.length;
  }
}

bool DepthMetaToMat(const visual::shm::ShmDepthMeta& meta, const std::uint8_t* blob, cv::Mat* out,
                    std::string* error) {
  if (out == nullptr || blob == nullptr || meta.width == 0 || meta.height == 0 ||
      meta.blob_size == 0) {
    if (error) {
      *error = "深度数据为空";
    }
    return false;
  }

  const int w = static_cast<int>(meta.width);
  const int h = static_cast<int>(meta.height);
  const auto format = static_cast<visual::DepthPixelFormat>(meta.format);

  auto make_continuous = [&](int cv_type, std::size_t elem_bytes) -> bool {
    const std::size_t expected = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * elem_bytes;
    if (meta.blob_size < expected) {
      if (error) {
        *error = "深度数据长度不足";
      }
      return false;
    }
    const std::uint32_t stride = meta.row_stride_bytes != 0
                                     ? meta.row_stride_bytes
                                     : static_cast<std::uint32_t>(w * elem_bytes);
    if (stride == static_cast<std::uint32_t>(w * elem_bytes)) {
      cv::Mat view(h, w, cv_type, const_cast<std::uint8_t*>(blob));
      *out = view.clone();
      return true;
    }
    *out = cv::Mat(h, w, cv_type);
    for (int r = 0; r < h; ++r) {
      const auto* src = blob + static_cast<std::size_t>(r) * stride;
      std::memcpy(out->ptr(r), src, static_cast<std::size_t>(w) * elem_bytes);
    }
    return true;
  };

  switch (format) {
    case visual::DepthPixelFormat::kUint16Mm:
      return make_continuous(CV_16UC1, sizeof(std::uint16_t));
    case visual::DepthPixelFormat::kFloat32Mm:
      return make_continuous(CV_32FC1, sizeof(float));
    case visual::DepthPixelFormat::kFloat64Mm: {
      cv::Mat d64;
      const std::size_t expected =
          static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * sizeof(double);
      if (meta.blob_size < expected) {
        if (error) {
          *error = "深度数据长度不足";
        }
        return false;
      }
      const std::uint32_t stride = meta.row_stride_bytes != 0
                                       ? meta.row_stride_bytes
                                       : static_cast<std::uint32_t>(w * sizeof(double));
      if (stride == static_cast<std::uint32_t>(w * sizeof(double))) {
        d64 = cv::Mat(h, w, CV_64FC1, const_cast<std::uint8_t*>(blob)).clone();
      } else {
        d64 = cv::Mat(h, w, CV_64FC1);
        for (int r = 0; r < h; ++r) {
          const auto* src = blob + static_cast<std::size_t>(r) * stride;
          std::memcpy(d64.ptr(r), src, static_cast<std::size_t>(w) * sizeof(double));
        }
      }
      d64.convertTo(*out, CV_32FC1);
      return true;
    }
    default:
      if (error) {
        *error = "不支持的深度格式";
      }
      return false;
  }
}

bool PickFirstDepth(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                    cv::Mat* depth, std::string* error) {
  for (std::int32_t i = 0; i < header->camera_count; ++i) {
    const auto& cam = header->cameras[i];
    if (cam.depth.blob_size == 0 || cam.depth.width == 0 || cam.depth.height == 0) {
      continue;
    }
    const auto* blob = blob_arena + cam.depth.blob_offset;
    if (DepthMetaToMat(cam.depth, blob, depth, error)) {
      return true;
    }
  }
  if (error && error->empty()) {
    *error = "未收到深度图";
  }
  return false;
}

/**
 * 从 SHM 解 Mono8 灰度 → CV_8UC1（与落盘 P5 .pgm 像素布局一致：行优先、无 padding）。
 * 返回相机下标，失败 -1。
 */
int PickFirstGray(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                  cv::Mat* gray) {
  if (header == nullptr || blob_arena == nullptr || gray == nullptr) {
    return -1;
  }
  for (std::int32_t i = 0; i < header->camera_count; ++i) {
    const auto& cam = header->cameras[i];
    if (cam.image.blob_size == 0 || cam.image.width == 0 || cam.image.height == 0) {
      continue;
    }
    // 请求侧约定仅传 Mono8；与 WriteMono8GrayPgm / algo_shm_codec 一致
    if (cam.image.format != visual::ImagePixelFormat::kMono8 || cam.image.bytes_per_pixel != 1) {
      continue;
    }
    const int w = static_cast<int>(cam.image.width);
    const int h = static_cast<int>(cam.image.height);
    const std::uint32_t stride = cam.image.row_stride_bytes != 0
                                     ? cam.image.row_stride_bytes
                                     : static_cast<std::uint32_t>(w);
    const std::size_t min_bytes =
        static_cast<std::size_t>(h - 1) * stride + static_cast<std::size_t>(w);
    if (cam.image.blob_size < min_bytes) {
      continue;
    }
    const auto* blob = blob_arena + cam.image.blob_offset;
    if (stride == static_cast<std::uint32_t>(w)) {
      // 连续缓冲：与 cv::imread(P5) / 落盘 gray.pgm 原始区相同
      *gray = cv::Mat(h, w, CV_8UC1, const_cast<std::uint8_t*>(blob)).clone();
    } else {
      *gray = cv::Mat(h, w, CV_8UC1);
      for (int r = 0; r < h; ++r) {
        std::memcpy(gray->ptr(r), blob + static_cast<std::size_t>(r) * stride,
                    static_cast<std::size_t>(w));
      }
    }
    return i;
  }
  return -1;
}

visual::ImagePixelFormat CvTypeToImageFormat(int cv_type) {
  switch (cv_type) {
    case CV_8UC1:
      return visual::ImagePixelFormat::kMono8;
    case CV_8UC3:
      return visual::ImagePixelFormat::kBgr8;
    case CV_8UC4:
      return visual::ImagePixelFormat::kBgra8;
    default:
      return visual::ImagePixelFormat::kNone;
  }
}

/** transferGray：把可视化图或本周期灰度写回 SHM，避免视口沿用上一周期残留。 */
bool WritePreviewImageToShm(visual::shm::ShmHeader* header, std::uint8_t* blob_arena,
                            std::size_t blob_arena_size, int cam_idx, const cv::Mat& image) {
  if (header == nullptr || blob_arena == nullptr || image.empty()) {
    return false;
  }
  if (!visual::HasTransferFlag(header->transfer_flags, visual::AlgoTransferFlag::kGray)) {
    return false;
  }
  cv::Mat cont = image.isContinuous() ? image : image.clone();
  const visual::ImagePixelFormat fmt = CvTypeToImageFormat(cont.type());
  if (fmt == visual::ImagePixelFormat::kNone) {
    AlgoError("可视化图像格式不支持 type=" + std::to_string(cont.type()));
    return false;
  }
  std::string write_err;
  if (!visual::shm::WriteResultImageToShm(
          header, blob_arena, blob_arena_size, cam_idx,
          static_cast<std::uint32_t>(cont.cols), static_cast<std::uint32_t>(cont.rows), fmt,
          cont.data, cont.total() * cont.elemSize(), &write_err)) {
    AlgoError("写回可视化图失败: " + write_err);
    return false;
  }
  return true;
}

std::filesystem::path ResolvePointCloudConfig(const PointCloudAlgoOptions& pc,
                                              const std::filesystem::path& exe_dir) {
  if (!pc.point_cloud_config.empty()) {
    std::filesystem::path p(pc.point_cloud_config);
    if (p.is_absolute()) {
      return p;
    }
    return exe_dir / p;
  }
  return exe_dir / "config.json";
}

}  // namespace

bool RunPointCloudFromShm(visual::shm::ShmHeader* header, std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, const AlgoConfig& config,
                          const std::filesystem::path& exe_dir, visual::shm::ShmLogResult* logs,
                          std::size_t log_count, std::string* error,
                          PointCloudProcessorSlot* slot,
                          const PointCloudAlgoOptions* channel_pc) {
  if (logs == nullptr || log_count == 0) {
    if (error) {
      *error = "结果缓冲区无效";
    }
    return false;
  }
  FillAllNg(logs, log_count);

  if (!visual::shm::ValidateShmRequest(header, blob_arena, blob_arena_size, error)) {
    return false;
  }

  cv::Mat depth;
  cv::Mat offline_gray;  // 仅 kOfflinePath 预读；在线仍从 SHM 取灰
  const bool offline_path =
      header->input_mode == static_cast<std::uint32_t>(visual::AlgoInputMode::kOfflinePath);

  if (!config.temp_force_depth_tiff.empty()) {
    // 调试覆盖：任意模式优先，且文件已是毫米
    if (!LoadDepthTiffFile(config.temp_force_depth_tiff, &depth, error)) {
      return false;
    }
    AlgoDebug("使用临时深度图: " + config.temp_force_depth_tiff);
  } else if (offline_path) {
    // 历史回放：只读磁盘，不走 SHM blob，避免影响在线 PickFirstDepth 通路
    const std::filesystem::path session_dir(header->session_dir);
    const std::string station_tag = StationTagFromShmId(header->station_id);
    const std::string camera_id = FirstCameraIdFromHeader(header);
    std::filesystem::path depth_path;
    if (!FindDepthFileInSession(session_dir, station_tag, camera_id, &depth_path, error)) {
      return false;
    }
    if (!LoadDepthTiffFile(depth_path, &depth, error)) {
      return false;
    }
    AlgoInfo("离线深度已加载: " + depth_path.string());

    std::filesystem::path gray_path;
    if (FindGrayFileInSession(session_dir, station_tag, camera_id, &gray_path)) {
      if (LoadGrayImageFile(gray_path, &offline_gray)) {
        AlgoDebug("离线灰度已加载: " + gray_path.string());
      }
    }
  } else {
    if (!PickFirstDepth(header, blob_arena, &depth, error)) {
      return false;
    }
    // SHM 浮点深度为米，换算毫米；与落盘 TIFF/PGM 路径刻意分开
    ConvertShmDepthMetersToMm(&depth);
  }

  if (depth.empty() || depth.cols <= 0 || depth.rows <= 0) {
    if (error) {
      *error = "深度图尺寸无效";
    }
    return false;
  }

  // 在线双通道传入 channel_pc；否则按 station_id 回退（离线/兼容）
  const PointCloudAlgoOptions& pc =
      channel_pc != nullptr ? *channel_pc
                            : PointCloudOptionsForStationId(config, header->station_id);
  if (!pc.use_point_cloud_algo) {
    if (error) {
      *error = "本工位未启用点云算法";
    }
    return false;
  }

  const auto cfg_path = ResolvePointCloudConfig(pc, exe_dir);
  PointCloudProcessorPtr owned;
  PointCloudProcessor* proc = nullptr;

  try {
    SanitizeDepthMm(&depth);
    const std::size_t positive_depth = CountPositiveDepthMm(depth);

    cv::Mat input_gray;
    int gray_cam_index = -1;
    const bool want_gray =
        config.transfer_gray ||
        visual::HasTransferFlag(header->transfer_flags, visual::AlgoTransferFlag::kGray);
    if (!offline_gray.empty()) {
      input_gray = offline_gray;
      gray_cam_index = 0;
    } else if (want_gray) {
      gray_cam_index = PickFirstGray(header, blob_arena, &input_gray);
    }

    if (positive_depth == 0) {
      // 空点云：不创建引擎；仍写回本周期灰度，避免视口停在上一帧标注图。
      if (want_gray && !input_gray.empty()) {
        WritePreviewImageToShm(header, blob_arena, blob_arena_size,
                               gray_cam_index >= 0 ? gray_cam_index : 0, input_gray);
      }
      AlgoInfo("计算完成 检出=0/" + std::to_string(log_count) + "（空点云跳过 process）");
      return true;
    }

    AlgoInfo("深度图 " + std::to_string(depth.cols) + "x" + std::to_string(depth.rows) +
             " 有效像素=" + std::to_string(positive_depth));

    if (slot != nullptr) {
      if (slot->processor != nullptr &&
          (slot->last_depth_w != depth.cols || slot->last_depth_h != depth.rows)) {
        // 运行中不析构旧实例；分辨率变化时请重启算法进程。
        if (error) {
          *error = "深度分辨率变化，请重启算法进程";
        }
        return false;
      }
      if (slot->processor == nullptr) {
        if (!std::filesystem::exists(cfg_path)) {
          if (error) {
            *error = "算法配置文件缺失: " + cfg_path.string();
          }
          return false;
        }
        std::string create_error;
        slot->processor = CreatePointCloudProcessorProtected(cfg_path.string(), &create_error);
        if (!slot->processor) {
          if (error) {
            *error =
                create_error.empty() ? ("算法引擎加载失败: " + cfg_path.string()) : create_error;
          }
          return false;
        }
        slot->last_depth_w = depth.cols;
        slot->last_depth_h = depth.rows;
        AlgoInfo("已加载算法引擎: " + cfg_path.string());
      }
      proc = slot->processor.get();
    } else {
      if (!std::filesystem::exists(cfg_path)) {
        if (error) {
          *error = "算法配置文件缺失: " + cfg_path.string();
        }
        return false;
      }
      std::string create_error;
      owned = CreatePointCloudProcessorProtected(cfg_path.string(), &create_error);
      if (!owned) {
        if (error) {
          *error = create_error.empty() ? ("算法引擎加载失败: " + cfg_path.string()) : create_error;
        }
        return false;
      }
      proc = owned.get();
    }

    const int top_n = pc.point_cloud_top_n > 0 ? pc.point_cloud_top_n : 5;
    cv::Mat draw_image = input_gray;
    if (draw_image.empty()) {
      draw_image = cv::Mat(depth.rows, depth.cols, CV_8UC1, cv::Scalar(0));
    }
    const cv::Mat input_gray_keep = draw_image.clone();

    // 样例契约：process(depth, gray) → getImage；不在此前调用 loadDepthMap。
    AlgoInfo("process 开始 top_n=" + std::to_string(top_n));
    const int n = PCP_Process(proc, depth, &draw_image, top_n);
    if (n == -999) {
      AlgoError("PointCloudProcessor::process 发生原生异常；SEH 后禁止 delete 引擎（防堆损坏），进程退出由视觉重启");
      if (error) {
        *error = "算法引擎原生异常，请检查深度数据与 config.json";
      }
#ifdef _WIN32
      ExitProcess(1);
#else
      std::_Exit(1);
#endif
    }

    const std::size_t point_count = PCP_GetPointCount(proc);
    AlgoInfo("process 结束 返回=" + std::to_string(n) + " 点云点数=" +
             std::to_string(point_count));

    std::size_t fit_n = 0;
    if (n > 0 && point_count > 0) {
      const auto fits = PCP_GetFitResults(proc);
      fit_n = fits.size();
      MapFitResultsToLogs(fits, logs, log_count);
    }

    int ok_n = 0;
    for (std::size_t i = 0; i < log_count; ++i) {
      if (logs[i].status == 1) {
        ++ok_n;
      }
    }

    // 有合格检出写标注图；无检出强制写本周期灰度，防止 getImage 残留上一周期画面。
    if (want_gray) {
      cv::Mat vis;
      if (ok_n > 0) {
        vis = draw_image;
        if (vis.empty()) {
          vis = PCP_GetImage(proc);
        }
      }
      if (vis.empty()) {
        vis = input_gray_keep;
      }
      WritePreviewImageToShm(header, blob_arena, blob_arena_size,
                             gray_cam_index >= 0 ? gray_cam_index : 0, vis);
    }

    AlgoInfo("计算完成 检出=" + std::to_string(ok_n) + "/" + std::to_string(log_count) +
             " 簇数=" + std::to_string(PCP_GetClusterCount(proc)));

    if (GetAlgoLogLevel() >= LogLevel::kDebug) {
      for (std::size_t i = 0; i < log_count; ++i) {
        const auto& L = logs[i];
        std::ostringstream oss;
        oss << "结果" << (i + 1) << " 状态=" << L.status << " X=" << L.offset_x_mm
            << " Y=" << L.offset_y_mm << " R=" << L.offset_r_deg << " 直径=" << L.diameter_mm
            << " 长度=" << L.length_mm;
        AlgoDebug(oss.str());
      }
      AlgoDebug("process返回=" + std::to_string(n) + " 拟合条数=" + std::to_string(fit_n));
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("算法异常: ") + ex.what();
    }
    return false;
  } catch (...) {
    if (error) {
      *error = "算法未知异常";
    }
    return false;
  }
}

}  // namespace algo

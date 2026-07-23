/**
 * @file algo_pointcloud_runner.cpp
 * @brief 点云算法：SHM/临时 TIFF 深度 → PointCloudProcessor → 5 条结果。
 *
 * SHM 深度单位为米，固定换算为毫米后再计算；临时 TIFF 已是毫米。
 */
#include "algo_pointcloud_runner.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "algo_log.h"
#include "visual/algo_shm_codec.h"
#include "visual/capture_data_format.h"

namespace algo {
namespace {

bool LoadDepthTiffFile(const std::filesystem::path& path, cv::Mat* out, std::string* error) {
  if (out == nullptr) {
    if (error) {
      *error = "临时深度图输出为空";
    }
    return false;
  }
  if (!std::filesystem::exists(path)) {
    if (error) {
      *error = "临时深度图不存在: " + path.string();
    }
    return false;
  }
  cv::Mat img = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
  if (img.empty()) {
    if (error) {
      *error = "临时深度图读取失败: " + path.string();
    }
    return false;
  }
  if (img.channels() != 1) {
    if (error) {
      *error = "临时深度图须为单通道";
    }
    return false;
  }
  if (img.type() == CV_16UC1 || img.type() == CV_64FC1) {
    img.convertTo(*out, CV_32FC1);
  } else if (img.type() == CV_32FC1) {
    *out = img;
  } else {
    if (error) {
      *error = "临时深度图格式不支持";
    }
    return false;
  }
  return true;
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

std::filesystem::path ResolvePointCloudConfig(const AlgoConfig& config,
                                              const std::filesystem::path& exe_dir) {
  if (!config.point_cloud_config.empty()) {
    std::filesystem::path p(config.point_cloud_config);
    if (p.is_absolute()) {
      return p;
    }
    return exe_dir / p;
  }
  return exe_dir / "config.json";
}

}  // namespace

bool RunPointCloudFromShm(const visual::shm::ShmHeader* header, const std::uint8_t* blob_arena,
                          std::size_t blob_arena_size, const AlgoConfig& config,
                          const std::filesystem::path& exe_dir, visual::shm::ShmLogResult* logs,
                          std::size_t log_count, std::string* error,
                          PointCloudProcessorSlot* slot) {
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
  if (!config.temp_force_depth_tiff.empty()) {
    if (!LoadDepthTiffFile(config.temp_force_depth_tiff, &depth, error)) {
      return false;
    }
    AlgoDebug("使用临时深度图: " + config.temp_force_depth_tiff);
  } else {
    if (!PickFirstDepth(header, blob_arena, &depth, error)) {
      return false;
    }
    ConvertShmDepthMetersToMm(&depth);
  }

  if (depth.empty() || depth.cols <= 0 || depth.rows <= 0) {
    if (error) {
      *error = "深度图尺寸无效";
    }
    return false;
  }

  const auto cfg_path = ResolvePointCloudConfig(config, exe_dir);
  PointCloudProcessorPtr owned;
  PCP_Handle* proc = nullptr;

  try {
    if (slot != nullptr) {
      const bool size_changed =
          slot->last_depth_w > 0 &&
          (slot->last_depth_w != depth.cols || slot->last_depth_h != depth.rows);
      if (slot->processor == nullptr || size_changed) {
        if (!std::filesystem::exists(cfg_path)) {
          if (error) {
            *error = "算法配置文件缺失: " + cfg_path.string();
          }
          return false;
        }
        if (size_changed) {
          std::ostringstream oss;
          oss << "深度分辨率变化 " << slot->last_depth_w << "x" << slot->last_depth_h << " -> "
              << depth.cols << "x" << depth.rows << "，重建算法引擎";
          AlgoInfo(oss.str());
        }
        std::string create_error;
        slot->processor = CreatePointCloudProcessorProtected(cfg_path.string(), &create_error);
        if (!slot->processor) {
          if (error) {
            *error = create_error.empty() ? ("算法引擎加载失败: " + cfg_path.string()) : create_error;
          }
          return false;
        }
        AlgoInfo("已加载算法引擎: " + cfg_path.string());
      }
      slot->last_depth_w = depth.cols;
      slot->last_depth_h = depth.rows;
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

    const int top_n = config.point_cloud_top_n > 0 ? config.point_cloud_top_n : 5;

    {
      std::ostringstream oss;
      oss << "深度图 " << depth.cols << "x" << depth.rows << " type=" << depth.type();
      AlgoInfo(oss.str());
    }

    if (!PCP_LoadDepthMap(proc, depth)) {
      if (error) {
        *error = "深度转点云失败（或引擎原生异常）";
      }
      return false;
    }
    const std::size_t point_count = PCP_GetPointCount(proc);
    AlgoInfo("点云点数=" + std::to_string(point_count));

    if (point_count == 0) {
      AlgoInfo("计算完成 检出=0/" + std::to_string(log_count) + " 簇数=0（空点云跳过）");
      return true;
    }

    AlgoInfo("process 开始 top_n=" + std::to_string(top_n));
    const int n = PCP_Process(proc, depth, top_n);
    if (n == -999) {
      AlgoError("PointCloudProcessor::process 发生原生异常（已捕获，进程继续）");
      if (error) {
        *error = "算法引擎原生异常，请检查深度数据与 config.json";
      }
      if (slot != nullptr) {
        slot->processor.reset();
        slot->last_depth_w = 0;
        slot->last_depth_h = 0;
      }
      return false;
    }
    AlgoInfo("process 结束 返回=" + std::to_string(n));

    std::size_t fit_n = 0;
    if (n > 0 && PCP_GetPointCount(proc) > 0) {
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

    {
      std::ostringstream oss;
      oss << "计算完成 检出=" << ok_n << "/" << log_count << " 簇数=" << PCP_GetClusterCount(proc);
      AlgoInfo(oss.str());
    }

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

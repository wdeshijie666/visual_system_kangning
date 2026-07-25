/**
 * @file algo_processor_create.cpp
 * @brief 唯一包含 PointCloudProcessor.h 的翻译单元；以 /EHa 捕获引擎 SEH。
 */
#include "algo_processor_create.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

// 直接使用算法库官方头，避免精简 shim 与 DLL 成员布局漂移（如 m_DrawImage）。
#include "PointCloudProcessor.h"

void PointCloudProcessorDeleter::operator()(PointCloudProcessor* p) const {
  delete p;
}

namespace {

bool ValidateConfigFile(const std::string& config_path, std::string* error) {
  std::ifstream in(config_path, std::ios::binary);
  if (!in) {
    if (error) {
      *error = "无法打开算法配置: " + config_path;
    }
    return false;
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  const std::string text = oss.str();
  if (text.empty()) {
    if (error) {
      *error = "算法配置为空: " + config_path;
    }
    return false;
  }
  try {
    const auto j = nlohmann::json::parse(text);
    if (!j.is_object()) {
      if (error) {
        *error = "算法配置不是 JSON 对象: " + config_path;
      }
      return false;
    }
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("算法配置 JSON 无效: ") + ex.what();
    }
    return false;
  }
  return true;
}

}  // namespace

PointCloudProcessorPtr CreatePointCloudProcessorProtected(const std::string& config_path,
                                                          std::string* error) {
  if (!ValidateConfigFile(config_path, error)) {
    return nullptr;
  }
  try {
    return PointCloudProcessorPtr(new PointCloudProcessor(config_path));
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("算法引擎构造异常: ") + ex.what();
    }
    return nullptr;
  } catch (...) {
    if (error) {
      *error = "算法引擎构造失败（原生异常）";
    }
    return nullptr;
  }
}

bool PCP_LoadDepthMap(PointCloudProcessor* p, const cv::Mat& depth) {
  if (p == nullptr || depth.empty()) {
    return false;
  }
  try {
    return p->loadDepthMap(depth);
  } catch (...) {
    return false;
  }
}

std::size_t PCP_GetPointCount(PointCloudProcessor* p) {
  if (p == nullptr) {
    return 0;
  }
  try {
    return p->getPointCount();
  } catch (...) {
    return 0;
  }
}

int PCP_Process(PointCloudProcessor* p, const cv::Mat& depth, cv::Mat* draw_image, int top_n) {
  if (p == nullptr || depth.empty()) {
    return -999;
  }
  try {
    // 样例契约：process(depth, gray) → getImage()；底图用独立副本，避免与引擎共享缓冲。
    cv::Mat gray_image;
    if (draw_image != nullptr && !draw_image->empty()) {
      gray_image = draw_image->clone();
    }
    cv::Mat empty;
    cv::Mat& draw_ref = gray_image.empty() ? empty : gray_image;

    const int n = p->process(depth, draw_ref, top_n);

    // 新 Mat 接住可视化图后按行拷出；getImage 为空时保留入参底图，供写回本周期灰度。
    if (n >= 0 && draw_image != nullptr) {
      cv::Mat image = p->getImage();
      if (!image.empty()) {
        draw_image->create(image.rows, image.cols, image.type());
        if (image.isContinuous() && draw_image->isContinuous()) {
          std::memcpy(draw_image->data, image.data, image.total() * image.elemSize());
        } else {
          for (int r = 0; r < image.rows; ++r) {
            std::memcpy(draw_image->ptr(r), image.ptr(r),
                        static_cast<std::size_t>(image.cols) * image.elemSize());
          }
        }
      }
    }
    return n;
  } catch (...) {
    return -999;
  }
}

cv::Mat PCP_GetImage(PointCloudProcessor* p) {
  if (p == nullptr) {
    return {};
  }
  try {
    return p->getImage();
  } catch (...) {
    return {};
  }
}

std::size_t PCP_GetClusterCount(PointCloudProcessor* p) {
  if (p == nullptr) {
    return 0;
  }
  try {
    return p->getClusters().size();
  } catch (...) {
    return 0;
  }
}

std::vector<PCP_FitResult> PCP_GetFitResults(PointCloudProcessor* p) {
  std::vector<PCP_FitResult> out;
  if (p == nullptr) {
    return out;
  }
  try {
    const auto& fits = p->getFitResults();
    out.reserve(fits.size());
    for (const auto& f : fits) {
      PCP_FitResult r;
      r.log_index = f.log_index;
      r.valid = f.valid;
      r.offset_x = f.offset_x;
      r.offset_y = f.offset_y;
      r.tilt_deg = f.tilt_deg;
      r.diameter = f.diameter;
      r.length = f.length;
      out.push_back(r);
    }
  } catch (...) {
    out.clear();
  }
  return out;
}

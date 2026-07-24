/**
 * @file algo_processor_create.cpp
 * @brief 唯一包含 PointCloudProcessor 精简头的翻译单元；以 /EHa 捕获引擎 SEH。
 */
#include "algo_processor_create.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "PointCloudProcessor_api.h"

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

int PCP_Process(PointCloudProcessor* p, const cv::Mat& depth, int top_n) {
  if (p == nullptr || depth.empty()) {
    return -999;
  }
  try {
    return p->process(depth, top_n);
  } catch (...) {
    return -999;
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

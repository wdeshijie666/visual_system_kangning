/**
 * @file algo_processor_create.cpp
 * @brief 通过 pcp_c_api（VS2022 编译）调用 PointCloudProcessor，避免 VS2019↔VS2022 std::string ABI 崩溃。
 */
#include "algo_processor_create.h"

#include "pcp_c_api/pcp_c_api.h"

void PointCloudProcessorDeleter::operator()(PCP_Handle* p) const {
  pcp_destroy(p);
}

PointCloudProcessorPtr CreatePointCloudProcessorProtected(const std::string& config_path,
                                                          std::string* error) {
  char err_buf[512] = {};
  PCP_Handle* handle = pcp_create(config_path.c_str(), err_buf, static_cast<int>(sizeof(err_buf)));
  if (handle == nullptr) {
    if (error) {
      *error = err_buf[0] != '\0'
                   ? std::string(err_buf)
                   : ("算法引擎加载失败: " + config_path);
    }
    return nullptr;
  }
  return PointCloudProcessorPtr(handle);
}

namespace {

bool EnsureContinuousDepth(const cv::Mat& depth, cv::Mat* continuous) {
  if (depth.empty() || continuous == nullptr) {
    return false;
  }
  if (depth.isContinuous()) {
    *continuous = depth;
  } else {
    *continuous = depth.clone();
  }
  return !continuous->empty();
}

}  // namespace

bool PCP_LoadDepthMap(PCP_Handle* p, const cv::Mat& depth) {
  cv::Mat cont;
  if (p == nullptr || !EnsureContinuousDepth(depth, &cont)) {
    return false;
  }
  return pcp_load_depth_map(p, cont.data, cont.rows, cont.cols, cont.type()) != 0;
}

std::size_t PCP_GetPointCount(PCP_Handle* p) {
  if (p == nullptr) {
    return 0;
  }
  return pcp_get_point_count(p);
}

int PCP_Process(PCP_Handle* p, const cv::Mat& depth, int top_n) {
  cv::Mat cont;
  if (p == nullptr || !EnsureContinuousDepth(depth, &cont)) {
    return -999;
  }
  return pcp_process(p, cont.data, cont.rows, cont.cols, cont.type(), top_n);
}

std::size_t PCP_GetClusterCount(PCP_Handle* p) {
  if (p == nullptr) {
    return 0;
  }
  return pcp_get_cluster_count(p);
}

std::vector<PCP_FitResult> PCP_GetFitResults(PCP_Handle* p) {
  std::vector<PCP_FitResult> out;
  if (p == nullptr) {
    return out;
  }
  PCP_C_FitResult buf[64];
  const int n = pcp_get_fit_results(p, buf, 64);
  out.reserve(static_cast<std::size_t>(n > 0 ? n : 0));
  for (int i = 0; i < n; ++i) {
    PCP_FitResult r;
    r.log_index = buf[i].log_index;
    r.valid = buf[i].valid != 0;
    r.offset_x = buf[i].offset_x;
    r.offset_y = buf[i].offset_y;
    r.tilt_deg = buf[i].tilt_deg;
    r.diameter = buf[i].diameter;
    r.length = buf[i].length;
    out.push_back(r);
  }
  return out;
}

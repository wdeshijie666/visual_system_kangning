/**
 * @file algo_processor_create.h
 * @brief 算法引擎创建/调用封装（经 pcp_c_api C ABI，跨 MSVC 工具集安全）。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

struct PCP_Handle;

struct PointCloudProcessorDeleter {
  void operator()(PCP_Handle* p) const;
};

using PointCloudProcessorPtr = std::unique_ptr<PCP_Handle, PointCloudProcessorDeleter>;

/** 构造算法引擎；失败返回空并写入 error。 */
PointCloudProcessorPtr CreatePointCloudProcessorProtected(const std::string& config_path,
                                                          std::string* error);

bool PCP_LoadDepthMap(PCP_Handle* p, const cv::Mat& depth);
std::size_t PCP_GetPointCount(PCP_Handle* p);
int PCP_Process(PCP_Handle* p, const cv::Mat& depth, int top_n);
std::size_t PCP_GetClusterCount(PCP_Handle* p);

struct PCP_FitResult {
  std::uint16_t log_index = 0;
  bool valid = false;
  double offset_x = 0;
  double offset_y = 0;
  double tilt_deg = 0;
  double diameter = 0;
  double length = 0;
};

std::vector<PCP_FitResult> PCP_GetFitResults(PCP_Handle* p);

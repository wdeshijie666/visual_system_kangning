/**
 * @file algo_processor_create.h
 * @brief 算法引擎创建与调用封装（唯一包含 PointCloudProcessor 声明的翻译单元入口）。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

class PointCloudProcessor;

struct PointCloudProcessorDeleter {
  void operator()(PointCloudProcessor* p) const;
};

using PointCloudProcessorPtr = std::unique_ptr<PointCloudProcessor, PointCloudProcessorDeleter>;

/** 构造引擎；失败返回空并写入 error。 */
PointCloudProcessorPtr CreatePointCloudProcessorProtected(const std::string& config_path,
                                                          std::string* error);

bool PCP_LoadDepthMap(PointCloudProcessor* p, const cv::Mat& depth);
std::size_t PCP_GetPointCount(PointCloudProcessor* p);
int PCP_Process(PointCloudProcessor* p, const cv::Mat& depth, int top_n);
std::size_t PCP_GetClusterCount(PointCloudProcessor* p);

struct PCP_FitResult {
  std::uint16_t log_index = 0;
  bool valid = false;
  double offset_x = 0;
  double offset_y = 0;
  double tilt_deg = 0;
  double diameter = 0;
  double length = 0;
};

std::vector<PCP_FitResult> PCP_GetFitResults(PointCloudProcessor* p);

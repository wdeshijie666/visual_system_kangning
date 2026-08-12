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

/**
 * 构造引擎；失败返回空并写入 error。
 * @param reference_point_path 参考点 JSON；空则交给算法库默认（通常为同目录
 *        reference_point.json）。文件可不存在，库侧会用默认参考点。
 */
PointCloudProcessorPtr CreatePointCloudProcessorProtected(
    const std::string& config_path, const std::string& reference_point_path, std::string* error);

bool PCP_LoadDepthMap(PointCloudProcessor* p, const cv::Mat& depth);
std::size_t PCP_GetPointCount(PointCloudProcessor* p);
/**
 * 跑圆柱拟合（调用顺序对齐样例：process(depth, gray) → getImage() 进新 Mat）。
 * @param draw_image 入参：非空时拷贝为底图传入 process（不就地复用该 Mat）；
 *                   出参：成功后写入新 Mat（getImage()），不与入参底图共享缓冲。
 *                   可为 nullptr（不传底图、也不取回可视化）。
 * @return process 返回值；原生异常为 -999。
 */
int PCP_Process(PointCloudProcessor* p, const cv::Mat& depth, cv::Mat* draw_image, int top_n);
std::size_t PCP_GetClusterCount(PointCloudProcessor* p);
/** 取最近一次 process 后的可视化图；失败返回空 Mat。 */
cv::Mat PCP_GetImage(PointCloudProcessor* p);

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

/**
 * @file PointCloudProcessor_api.h
 * @brief 精简声明：不包含 pcl_visualizer，避免把 VTK 静态初始化链进调用方。
 *        成员布局须与 G:/ReconDLL/point_cloud/PointCloudProcessor.h 保持一致。
 */
#pragma once

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/PointIndices.h>
#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct CylinderFitResult {
  uint16_t log_index = 0;
  bool valid = false;
  double offset_x = 0;
  double offset_y = 0;
  double offset_z = 0;
  double normal_x = 0;
  double normal_y = 0;
  double normal_z = 0;
  double axis_x = 0;
  double axis_y = 0;
  double axis_z = 0;
  double diameter = 0;
  double length = 0;
  double tilt_deg = 0;
};

struct CylinderRange {
  double startx;
  double starty;
  double width;
  double height;
};

class PointCloudProcessor {
 public:
  using PointT = pcl::PointXYZ;
  using CloudT = pcl::PointCloud<PointT>;
  using CloudPtr = pcl::PointCloud<PointT>::Ptr;

  explicit PointCloudProcessor(const std::string& config_path = "config.json");
  ~PointCloudProcessor() = default;

  PointCloudProcessor(const PointCloudProcessor&) = delete;
  PointCloudProcessor& operator=(const PointCloudProcessor&) = delete;

  bool loadPLY(const std::string& file_path);
  bool loadXYZ(const std::string& file_path);

 private:
  void preprocess(CloudPtr& cloud) const;

 public:
  double z_filter_min = 400.0;
  double z_filter_max = 500.0;
  int downsample_target = 100000;
  float cluster_tolerance = 1.0f;
  int min_cluster_size = 1000;
  int max_cluster_size = 250000;

  int process(const cv::Mat& depth_map, int top_n = 5);
  const std::vector<CloudPtr>& getClusters() const { return m_clusters; }
  CloudPtr getLastCloud() const { return m_last_cloud; }
  std::size_t getPointCount() const;
  bool getZRange(double& min_z, double& max_z) const;

  double cylinder_radius_min = 10.0;
  double cylinder_radius_max = 100.0;
  double cylinder_inlier_ratio = 0.20;
  double cylinder_normal_z_min = 0.6;
  void validateClusters();
  const std::vector<CylinderFitResult>& getFitResults() const { return m_fit_results; }

  std::size_t transform();
  std::size_t transform(const cv::Mat& R, const cv::Mat& T);

  bool getXYBoundingBox(const pcl::PointIndices& indices, CylinderRange& bbox) const;
  const std::vector<CylinderRange>& getPreLocations() const { return m_pre_location; }
  bool savePreLocations(const std::string& path) const;
  bool loadPreLocations(const std::string& path);
  bool loadConfig(const std::string& path);

  const cv::Mat& getCameraMatrix() const { return m_intrinsics; }
  const cv::Mat& getDistortion() const { return m_distortion; }
  const cv::Mat& getRotation() const { return m_R; }
  const cv::Mat& getTranslation() const { return m_T; }

  bool loadDepthMap(const cv::Mat& depth);
  bool show(const std::string& window_title = "Point Cloud Viewer") const;

 private:
  CloudPtr m_last_cloud;
  CloudPtr m_original_cloud;
  std::vector<CloudPtr> m_clusters;
  std::vector<CylinderFitResult> m_fit_results;
  std::vector<CylinderRange> m_pre_location;
  cv::Mat m_intrinsics;
  cv::Mat m_distortion;
  cv::Mat m_R;
  cv::Mat m_T;
};

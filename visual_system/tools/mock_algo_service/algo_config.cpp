/**
 * @file algo_config.cpp
 * @brief 加载 algo_config.json；点云参数按 channels.r05/r09 区分，顶层作兼容默认。
 */
#include "algo_config.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace algo {
namespace {

using visual::algo_config::kModeOfflineReplay;
using visual::algo_config::kModeOnline;

std::filesystem::path NormalizePath(const std::filesystem::path& base, const std::string& configured) {
  std::filesystem::path path(configured);
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return (base / path).lexically_normal();
}

int ParseModeValue(const nlohmann::json& j) {
  if (j.contains("mode")) {
    if (j["mode"].is_number_integer()) {
      return j["mode"].get<int>();
    }
    if (j["mode"].is_string()) {
      const std::string mode = j["mode"].get<std::string>();
      if (mode == "offline_replay") {
        return kModeOfflineReplay;
      }
    }
  }
  return kModeOnline;
}

void ParsePipelineSimulation(const nlohmann::json& j, PipelineSimulationOptions* out) {
  if (out == nullptr || !j.contains("pipelineSimulation")) {
    return;
  }
  const auto& sim = j["pipelineSimulation"];
  out->enabled = sim.value("enabled", false);
  out->image_width = sim.value("imageWidth", out->image_width);
  out->image_height = sim.value("imageHeight", out->image_height);
  if (sim.contains("algoResult")) {
    const auto& r = sim["algoResult"];
    out->status = r.value("status", out->status);
    out->offset_x_mm = r.value("offsetX", out->offset_x_mm);
    out->offset_y_mm = r.value("offsetY", out->offset_y_mm);
    out->offset_r_deg = r.value("offsetR", out->offset_r_deg);
    out->diameter_mm = r.value("diameter", out->diameter_mm);
    out->length_mm = r.value("length", out->length_mm);
  }
}

void NormalizeTopN(PointCloudAlgoOptions* pc) {
  if (pc == nullptr) {
    return;
  }
  if (pc->point_cloud_top_n < 1) {
    pc->point_cloud_top_n = 5;
  }
}

/** 从 JSON 对象读点云三项；缺省字段保留 defaults。 */
void ParsePointCloudAlgoOptions(const nlohmann::json& j, const PointCloudAlgoOptions& defaults,
                                PointCloudAlgoOptions* out) {
  if (out == nullptr) {
    return;
  }
  *out = defaults;
  if (!j.is_object()) {
    NormalizeTopN(out);
    return;
  }
  out->use_point_cloud_algo = j.value("usePointCloudAlgo", defaults.use_point_cloud_algo);
  out->point_cloud_config = j.value("pointCloudConfig", defaults.point_cloud_config);
  out->point_cloud_top_n = j.value("pointCloudTopN", defaults.point_cloud_top_n);
  NormalizeTopN(out);
}

void ParseChannelObject(const nlohmann::json& c, const PointCloudAlgoOptions& pc_defaults,
                        AlgoChannelConfig* out) {
  if (out == nullptr || !c.is_object()) {
    return;
  }
  out->enabled = c.value("enabled", out->enabled);
  out->shm_name = c.value("shmName", out->shm_name);
  out->mutex_name = c.value("mutexName", out->mutex_name);
  ParsePointCloudAlgoOptions(c, pc_defaults, &out->point_cloud);
}

}  // namespace

std::filesystem::path ResolveConfigPath(const std::filesystem::path& exe_dir, const std::string& configured) {
  return NormalizePath(exe_dir, configured);
}

const PointCloudAlgoOptions& PointCloudOptionsForChannel(const AlgoConfig& config,
                                                        visual::shm::ShmChannelId channel) {
  return channel == visual::shm::ShmChannelId::kR09 ? config.channel_r09.point_cloud
                                                   : config.channel_r05.point_cloud;
}

const PointCloudAlgoOptions& PointCloudOptionsForStationId(const AlgoConfig& config,
                                                          std::int32_t station_id) {
  if (station_id == static_cast<std::int32_t>(visual::StationId::kR09)) {
    return config.channel_r09.point_cloud;
  }
  return config.channel_r05.point_cloud;
}

bool AnyChannelUsesPointCloudAlgo(const AlgoConfig& config) {
  return config.channel_r05.point_cloud.use_point_cloud_algo ||
         config.channel_r09.point_cloud.use_point_cloud_algo;
}

AlgoConfig LoadAlgoConfig(const std::filesystem::path& exe_dir) {
  AlgoConfig config;
  // 两工位先填相同默认，再被 JSON 覆盖
  config.channel_r05.point_cloud = config.point_cloud_defaults;
  config.channel_r09.point_cloud = config.point_cloud_defaults;

  const auto config_file = exe_dir / "algo_config.json";
  if (!std::filesystem::exists(config_file)) {
    config.offline_replay.data_dir = exe_dir / "offline_data";
    config.offline_replay.result_dir = exe_dir / "offline_result";
    return config;
  }

  std::ifstream in(config_file);
  if (!in.is_open()) {
    return config;
  }

  nlohmann::json j;
  in >> j;
  if (ParseModeValue(j) == kModeOfflineReplay) {
    config.mode = RunMode::kOfflineReplay;
  }

  if (j.contains("offlineReplay")) {
    const auto& replay = j["offlineReplay"];
    config.offline_replay.data_dir =
        NormalizePath(exe_dir, replay.value("dataDir", "./offline_data"));
    config.offline_replay.result_dir =
        NormalizePath(exe_dir, replay.value("resultDir", "./offline_result"));
  } else {
    config.offline_replay.data_dir = exe_dir / "offline_data";
    config.offline_replay.result_dir = exe_dir / "offline_result";
  }
  ParsePipelineSimulation(j, &config.pipeline_simulation);
  config.debug_save_depth = j.value("debugSaveDepth", false);
  config.debug_save_pointcloud = j.value("debugSavePointcloud", false);
  config.transfer_depth = j.value("transferDepth", config.transfer_depth);
  config.transfer_pointcloud = j.value("transferPointcloud", config.transfer_pointcloud);
  config.transfer_gray = j.value("transferGray", config.transfer_gray);
  config.temp_force_depth_tiff = j.value("tempForceDepthTiff", config.temp_force_depth_tiff);
  config.log_level = ParseLogLevel(j.value("logLevel", std::string("info")));

  // 顶层三项：旧配置兼容默认值
  ParsePointCloudAlgoOptions(j, config.point_cloud_defaults, &config.point_cloud_defaults);
  config.channel_r05.point_cloud = config.point_cloud_defaults;
  config.channel_r09.point_cloud = config.point_cloud_defaults;

  if (j.contains("channels")) {
    if (j["channels"].contains("r05")) {
      ParseChannelObject(j["channels"]["r05"], config.point_cloud_defaults, &config.channel_r05);
    }
    if (j["channels"].contains("r09")) {
      ParseChannelObject(j["channels"]["r09"], config.point_cloud_defaults, &config.channel_r09);
    }
  }
  return config;
}

bool SaveAlgoOnlineMode(const std::filesystem::path& exe_dir) {
  const auto config_file = exe_dir / "algo_config.json";
  if (!std::filesystem::exists(config_file)) {
    return true;
  }

  std::ifstream in(config_file);
  if (!in.is_open()) {
    return false;
  }
  nlohmann::json j;
  in >> j;
  in.close();

  if (ParseModeValue(j) != kModeOfflineReplay) {
    return true;
  }

  j["mode"] = kModeOnline;
  std::ofstream out(config_file);
  if (!out.is_open()) {
    return false;
  }
  out << j.dump(2);
  return out.good();
}

}  // namespace algo

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

}  // namespace

std::filesystem::path ResolveConfigPath(const std::filesystem::path& exe_dir, const std::string& configured) {
  return NormalizePath(exe_dir, configured);
}

AlgoConfig LoadAlgoConfig(const std::filesystem::path& exe_dir) {
  AlgoConfig config;
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
  config.use_point_cloud_algo = j.value("usePointCloudAlgo", config.use_point_cloud_algo);
  config.point_cloud_config = j.value("pointCloudConfig", config.point_cloud_config);
  config.point_cloud_top_n = j.value("pointCloudTopN", config.point_cloud_top_n);
  config.temp_force_depth_tiff = j.value("tempForceDepthTiff", config.temp_force_depth_tiff);
  config.log_level = ParseLogLevel(j.value("logLevel", std::string("info")));
  if (config.point_cloud_top_n < 1) {
    config.point_cloud_top_n = 5;
  }

  if (j.contains("channels")) {
    if (j["channels"].contains("r05")) {
      const auto& c = j["channels"]["r05"];
      config.channel_r05.enabled = c.value("enabled", true);
      config.channel_r05.shm_name = c.value("shmName", config.channel_r05.shm_name);
      config.channel_r05.mutex_name = c.value("mutexName", config.channel_r05.mutex_name);
    }
    if (j["channels"].contains("r09")) {
      const auto& c = j["channels"]["r09"];
      config.channel_r09.enabled = c.value("enabled", true);
      config.channel_r09.shm_name = c.value("shmName", config.channel_r09.shm_name);
      config.channel_r09.mutex_name = c.value("mutexName", config.channel_r09.mutex_name);
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

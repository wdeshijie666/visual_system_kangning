/**
 * @file app_context.cpp
 */
#include "visual/app_context.h"

#include <filesystem>
#include <fstream>

#include <QCoreApplication>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace visual {

AppContext& AppContext::Instance() {
  static AppContext ctx;
  return ctx;
}

std::string AppContext::ConfigPath() const {
  const QCoreApplication* app = QCoreApplication::instance();
  if (app != nullptr) {
    return (fs::path(app->applicationDirPath().toStdWString()) / "config").string();
  }
  return "./config";
}

AppSettings& AppContext::Settings() {
  return settings_;
}

const AppSettings& AppContext::Settings() const {
  return settings_;
}

const std::map<std::string, DeviceEntry>& AppContext::Devices() const {
  return devices_;
}

void AppContext::SetDevices(const std::map<std::string, DeviceEntry>& devices) {
  devices_ = devices;
}

bool AppContext::Load() {
  fs::create_directories(ConfigPath());
  const std::string setting_file = ConfigPath() + "/setting.json";
  if (!fs::exists(setting_file)) {
    settings_.station_r05.camera_ids = {"rvc_01"};
    settings_.station_r05.robots = {"R05", "R07"};
    settings_.station_r09.camera_ids = {"rvc_02"};
    settings_.station_r09.robots = {"R09"};
    return Save();
  }

  std::ifstream in(setting_file);
  if (!in.is_open()) {
    return false;
  }
  json j;
  in >> j;

  settings_.app_name = j.value("appName", settings_.app_name);
  settings_.logo_path = j.value("logoPath", settings_.logo_path);
  settings_.data_path = j.value("dataPath", settings_.data_path);
  if (j.contains("plc")) {
    settings_.plc.gateway = j["plc"].value("gateway", settings_.plc.gateway);
    settings_.plc.path = j["plc"].value("path", settings_.plc.path);
    if (j["plc"].contains("tags")) {
      settings_.plc.tag_camera_to_plc =
          j["plc"]["tags"].value("cameraToPlc", settings_.plc.tag_camera_to_plc);
      settings_.plc.tag_plc_to_camera =
          j["plc"]["tags"].value("plcToCamera", settings_.plc.tag_plc_to_camera);
    }
  }
  if (j.contains("stations")) {
    if (j["stations"].contains("r05")) {
      settings_.station_r05.camera_ids =
          j["stations"]["r05"].value("cameras", settings_.station_r05.camera_ids);
      settings_.station_r05.robots =
          j["stations"]["r05"].value("robots", settings_.station_r05.robots);
    }
    if (j["stations"].contains("r09")) {
      settings_.station_r09.camera_ids =
          j["stations"]["r09"].value("cameras", settings_.station_r09.camera_ids);
      settings_.station_r09.robots =
          j["stations"]["r09"].value("robots", settings_.station_r09.robots);
    }
  }
  if (j.contains("algo")) {
    settings_.algo_shm_name = j["algo"].value("shmName", settings_.algo_shm_name);
    settings_.algo_timeout_ms = j["algo"].value("timeoutMs", settings_.algo_timeout_ms);
    settings_.use_shm_algo = j["algo"].value("useShm", settings_.use_shm_algo);
    settings_.algo_transfer_depth = j["algo"].value("transferDepth", settings_.algo_transfer_depth);
    settings_.algo_transfer_pointcloud =
        j["algo"].value("transferPointcloud", settings_.algo_transfer_pointcloud);
    settings_.algo_debug_save_depth =
        j["algo"].value("debugSaveDepth", settings_.algo_debug_save_depth);
    settings_.algo_debug_save_pointcloud =
        j["algo"].value("debugSavePointcloud", settings_.algo_debug_save_pointcloud);
    settings_.algo_program_dir = j["algo"].value("programDir", settings_.algo_program_dir);
    settings_.algo_program_exe = j["algo"].value("programExe", settings_.algo_program_exe);
  }
  if (j.contains("dataStub")) {
    settings_.stub_save_depth = j["dataStub"].value("saveDepth", settings_.stub_save_depth);
    settings_.stub_save_pointcloud =
        j["dataStub"].value("savePointcloud", settings_.stub_save_pointcloud);
  }
  if (j.contains("runMode")) {
    settings_.run_mode = ParseRunMode(j.value("runMode", RunModeToString(settings_.run_mode)));
  }
  if (j.contains("simulation")) {
    const auto& sim = j["simulation"];
    settings_.simulation.image_width = sim.value("imageWidth", settings_.simulation.image_width);
    settings_.simulation.image_height = sim.value("imageHeight", settings_.simulation.image_height);
    if (sim.contains("algoResult")) {
      const auto& r = sim["algoResult"];
      settings_.simulation.algo_result.status = r.value("status", settings_.simulation.algo_result.status);
      settings_.simulation.algo_result.offset_x_mm =
          r.value("offsetX", settings_.simulation.algo_result.offset_x_mm);
      settings_.simulation.algo_result.offset_y_mm =
          r.value("offsetY", settings_.simulation.algo_result.offset_y_mm);
      settings_.simulation.algo_result.offset_r_deg =
          r.value("offsetR", settings_.simulation.algo_result.offset_r_deg);
      settings_.simulation.algo_result.diameter_mm =
          r.value("diameter", settings_.simulation.algo_result.diameter_mm);
      settings_.simulation.algo_result.length_mm =
          r.value("length", settings_.simulation.algo_result.length_mm);
    }
  }

  const std::string devices_file = ConfigPath() + "/devices.json";
  if (fs::exists(devices_file)) {
    std::ifstream din(devices_file);
    json dj;
    din >> dj;
    devices_.clear();
    if (dj.contains("cameras") && dj["cameras"].is_array()) {
      for (const auto& c : dj["cameras"]) {
        DeviceEntry e;
        e.id = c.value("id", "");
        e.type = c.value("type", "rvc");
        e.serial = c.value("serial", "");
        e.station = c.value("station", "r05");
        if (!e.id.empty()) {
          devices_[e.id] = e;
        }
      }
    }
  }
  return true;
}

bool AppContext::Save() {
  fs::create_directories(ConfigPath());
  json j;
  j["appName"] = settings_.app_name;
  j["logoPath"] = settings_.logo_path;
  j["dataPath"] = settings_.data_path;
  j["plc"]["gateway"] = settings_.plc.gateway;
  j["plc"]["path"] = settings_.plc.path;
  j["plc"]["tags"]["cameraToPlc"] = settings_.plc.tag_camera_to_plc;
  j["plc"]["tags"]["plcToCamera"] = settings_.plc.tag_plc_to_camera;
  j["stations"]["r05"]["cameras"] = settings_.station_r05.camera_ids;
  j["stations"]["r05"]["robots"] = settings_.station_r05.robots;
  j["stations"]["r09"]["cameras"] = settings_.station_r09.camera_ids;
  j["stations"]["r09"]["robots"] = settings_.station_r09.robots;
  j["algo"]["shmName"] = settings_.algo_shm_name;
  j["algo"]["timeoutMs"] = settings_.algo_timeout_ms;
  j["algo"]["useShm"] = settings_.use_shm_algo;
  j["algo"]["transferDepth"] = settings_.algo_transfer_depth;
  j["algo"]["transferPointcloud"] = settings_.algo_transfer_pointcloud;
  j["algo"]["debugSaveDepth"] = settings_.algo_debug_save_depth;
  j["algo"]["debugSavePointcloud"] = settings_.algo_debug_save_pointcloud;
  j["algo"]["programDir"] = settings_.algo_program_dir;
  j["algo"]["programExe"] = settings_.algo_program_exe;
  j["dataStub"]["saveDepth"] = settings_.stub_save_depth;
  j["dataStub"]["savePointcloud"] = settings_.stub_save_pointcloud;
  j["runMode"] = RunModeToString(settings_.run_mode);
  j["simulation"]["imageWidth"] = settings_.simulation.image_width;
  j["simulation"]["imageHeight"] = settings_.simulation.image_height;
  j["simulation"]["algoResult"]["status"] = settings_.simulation.algo_result.status;
  j["simulation"]["algoResult"]["offsetX"] = settings_.simulation.algo_result.offset_x_mm;
  j["simulation"]["algoResult"]["offsetY"] = settings_.simulation.algo_result.offset_y_mm;
  j["simulation"]["algoResult"]["offsetR"] = settings_.simulation.algo_result.offset_r_deg;
  j["simulation"]["algoResult"]["diameter"] = settings_.simulation.algo_result.diameter_mm;
  j["simulation"]["algoResult"]["length"] = settings_.simulation.algo_result.length_mm;

  std::ofstream out(ConfigPath() + "/setting.json");
  if (!out.is_open()) {
    return false;
  }
  out << j.dump(2);

  json dj;
  dj["cameras"] = json::array();
  for (const auto& kv : devices_) {
    json c;
    c["id"] = kv.second.id;
    c["type"] = kv.second.type;
    c["serial"] = kv.second.serial;
    c["station"] = kv.second.station;
    dj["cameras"].push_back(c);
  }
  std::ofstream dout(ConfigPath() + "/devices.json");
  if (!dout.is_open()) {
    return false;
  }
  dout << dj.dump(2);
  return true;
}

}  // namespace visual

/**
 * @file run_mode.h
 * @brief 视觉系统运行模式：实机 / 本地仿真。
 */
#pragma once

#include <string>

namespace visual {

enum class RunMode { kProduction, kSimulation };

/** 仿真通路下算法返回的 Log 模板（5 条 Log 默认相同）。 */
struct SimulationResultProfile {
  int status = 1;
  double offset_x_mm = 0.1;
  double offset_y_mm = 0.0;
  double offset_r_deg = 0.0;
  double diameter_mm = 100.0;
  double length_mm = 800.0;
};

struct SimulationSettings {
  int image_width = 2040;
  int image_height = 1080;
  SimulationResultProfile algo_result;
};

inline RunMode ParseRunMode(const std::string& text) {
  if (text == "simulation" || text == "sim") {
    return RunMode::kSimulation;
  }
  return RunMode::kProduction;
}

inline const char* RunModeToString(RunMode mode) {
  return mode == RunMode::kSimulation ? "simulation" : "production";
}

}  // namespace visual

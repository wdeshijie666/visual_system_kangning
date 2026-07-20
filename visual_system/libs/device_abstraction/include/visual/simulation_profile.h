/**
 * @file simulation_profile.h
 * @brief 进程内仿真结果配置（MockAlgoService 等读取）。
 */
#pragma once

#include "visual/run_mode.h"

namespace visual {

struct SimulationProfileState {
  bool enabled = false;
  SimulationResultProfile result{};
};

inline SimulationProfileState& SimulationProfileInstance() {
  static SimulationProfileState state;
  return state;
}

inline void SetSimulationProfile(bool enabled, const SimulationResultProfile& result) {
  auto& state = SimulationProfileInstance();
  state.enabled = enabled;
  state.result = result;
}

inline bool IsSimulationProfileEnabled() {
  return SimulationProfileInstance().enabled;
}

inline SimulationResultProfile GetSimulationResultProfile() {
  return SimulationProfileInstance().result;
}

}  // namespace visual

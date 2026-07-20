#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>

#include "algo_config.h"
#include "visual/algo_shm_layout.h"

namespace algo {

inline bool WriteAlgoResultCsv(const std::filesystem::path& file_path,
                               const visual::shm::ShmLogResult* logs, std::size_t count) {
  std::ostringstream line;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) {
      line << ',';
    }
    line << logs[i].status << ','
         << logs[i].offset_x_mm << ','
         << logs[i].offset_y_mm << ','
         << logs[i].offset_r_deg << ','
         << logs[i].diameter_mm << ','
         << logs[i].length_mm;
  }

  std::ofstream out(file_path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out << line.str() << '\n';
  return out.good();
}

inline void FillMockLogs(visual::shm::ShmLogResult* logs, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    logs[i].status = 1;
    logs[i].offset_x_mm = 0.1 * static_cast<double>(i + 1);
    logs[i].offset_y_mm = 0.0;
    logs[i].offset_r_deg = 0.0;
    logs[i].diameter_mm = 100.0;
    logs[i].length_mm = 900.0;
  }
}

inline void FillPipelineSimulationLogs(visual::shm::ShmLogResult* logs, std::size_t count,
                                       const PipelineSimulationOptions& sim) {
  for (std::size_t i = 0; i < count; ++i) {
    logs[i].status = sim.status;
    logs[i].offset_x_mm = sim.offset_x_mm;
    logs[i].offset_y_mm = sim.offset_y_mm;
    logs[i].offset_r_deg = sim.offset_r_deg;
    logs[i].diameter_mm = sim.diameter_mm;
    logs[i].length_mm = sim.length_mm;
  }
}

}  // namespace algo

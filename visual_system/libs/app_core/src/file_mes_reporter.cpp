/**
 * @file file_mes_reporter.cpp
 */
#include "visual/file_mes_reporter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "visual/rotating_file_log.h"

namespace fs = std::filesystem;

namespace visual {
namespace {

std::string NowIso() {
  using clock = std::chrono::system_clock;
  const auto t = clock::to_time_t(clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return oss.str();
}

}  // namespace

FileMesReporter::FileMesReporter(std::string file_path) : file_path_(std::move(file_path)) {}

void FileMesReporter::ReportCycle(const std::string& vin, StationId station, const LogResultBatch& logs) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    const fs::path path(file_path_);
    if (path.has_parent_path()) {
      fs::create_directories(path.parent_path());
    }
    RotatingFileLog::RotateBySize(file_path_, 8ull * 1024 * 1024, 9);
    std::ofstream out(file_path_, std::ios::app);
    if (!out) {
      return;
    }
    out << "{\"type\":\"cycle\",\"ts\":\"" << NowIso() << "\",\"vin\":\"" << vin
        << "\",\"station\":" << static_cast<int>(station) << ",\"logs\":[";
    for (std::size_t i = 0; i < logs.size(); ++i) {
      if (i > 0) {
        out << ',';
      }
      out << "{\"status\":" << static_cast<int>(logs[i].status) << ",\"x\":" << logs[i].offset_x_mm
          << ",\"y\":" << logs[i].offset_y_mm << ",\"r\":" << logs[i].offset_r_deg
          << ",\"d\":" << logs[i].diameter_mm << ",\"l\":" << logs[i].length_mm << '}';
    }
    out << "]}\n";
  } catch (...) {
  }
}

void FileMesReporter::ReportFault(const std::string& subsystem, const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    const fs::path path(file_path_);
    if (path.has_parent_path()) {
      fs::create_directories(path.parent_path());
    }
    RotatingFileLog::RotateBySize(file_path_, 8ull * 1024 * 1024, 9);
    std::ofstream out(file_path_, std::ios::app);
    if (!out) {
      return;
    }
    out << "{\"type\":\"fault\",\"ts\":\"" << NowIso() << "\",\"subsystem\":\"" << subsystem
        << "\",\"message\":\"" << message << "\"}\n";
  } catch (...) {
  }
}

}  // namespace visual

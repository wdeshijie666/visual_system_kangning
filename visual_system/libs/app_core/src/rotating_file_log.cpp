/**
 * @file rotating_file_log.cpp
 */
#include "visual/rotating_file_log.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace visual {

RotatingFileLog::RotatingFileLog(std::string file_path, std::size_t max_bytes)
    : file_path_(std::move(file_path)), max_bytes_(max_bytes == 0 ? 1024 * 1024 : max_bytes) {}

void RotatingFileLog::Append(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    const fs::path path(file_path_);
    if (path.has_parent_path()) {
      fs::create_directories(path.parent_path());
    }
    RotateIfNeededUnlocked();
    std::ofstream out(file_path_, std::ios::app);
    if (out.is_open()) {
      out << line << '\n';
    }
  } catch (...) {
    // 磁盘异常时静默，避免日志拖垮主流程
  }
}

void RotatingFileLog::RotateIfNeededUnlocked() {
  std::error_code ec;
  if (!fs::exists(file_path_, ec)) {
    return;
  }
  const auto size = fs::file_size(file_path_, ec);
  if (ec || size < max_bytes_) {
    return;
  }
  const std::string bak = file_path_ + ".1.bak";
  fs::remove(bak, ec);
  fs::rename(file_path_, bak, ec);
}

}  // namespace visual

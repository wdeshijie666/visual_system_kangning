/**
 * @file rotating_file_log.cpp
 */
#include "visual/rotating_file_log.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace visual {
namespace {

std::string BackupPath(const std::string& file_path, int index) {
  return file_path + "." + std::to_string(index) + ".bak";
}

}  // namespace

RotatingFileLog::RotatingFileLog(std::string file_path, std::size_t max_bytes, int max_backup_index)
    : file_path_(std::move(file_path)),
      max_bytes_(max_bytes == 0 ? 1024 * 1024 : max_bytes),
      max_backup_index_(max_backup_index < 1 ? 1 : max_backup_index) {}

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

void RotatingFileLog::RotateBySize(const std::string& file_path, std::size_t max_bytes,
                                   int max_backup_index) {
  std::error_code ec;
  if (!fs::exists(file_path, ec)) {
    return;
  }
  const auto size = fs::file_size(file_path, ec);
  if (ec || size < max_bytes) {
    return;
  }
  const int n = max_backup_index < 1 ? 1 : max_backup_index;
  // .8.bak → .9.bak … .1.bak → .2.bak，再 current → .1.bak（最旧 .9.bak 被覆盖）
  fs::remove(BackupPath(file_path, n), ec);
  for (int i = n - 1; i >= 1; --i) {
    const std::string from = BackupPath(file_path, i);
    const std::string to = BackupPath(file_path, i + 1);
    if (fs::exists(from, ec)) {
      fs::rename(from, to, ec);
    }
  }
  fs::rename(file_path, BackupPath(file_path, 1), ec);
}

void RotatingFileLog::RotateIfNeededUnlocked() {
  RotateBySize(file_path_, max_bytes_, max_backup_index_);
}

}  // namespace visual

/**
 * @file rotating_file_log.h
 * @brief 按大小滚动的文本日志（可复用，无 Qt 依赖）。
 */
#pragma once

#include <cstddef>
#include <mutex>
#include <string>

namespace visual {

class RotatingFileLog {
 public:
  /**
   * @param file_path 日志文件路径（如 ./logs/visual_system.log）
   * @param max_bytes 单文件上限，超出后滚到 .1.bak
   */
  RotatingFileLog(std::string file_path, std::size_t max_bytes = 8 * 1024 * 1024);

  void Append(const std::string& line);

 private:
  void RotateIfNeededUnlocked();

  std::string file_path_;
  std::size_t max_bytes_;
  std::mutex mutex_;
};

}  // namespace visual

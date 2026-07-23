/**
 * @file rotating_file_log.h
 * @brief 按大小滚动的文本日志（可复用，无 Qt 依赖）。
 *
 * 单文件上限默认 8MB；超出后滚动为 .1.bak … .9.bak（共最多 10 个文件：当前 + 9 备份）。
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
   * @param max_bytes 单文件上限
   * @param max_backup_index 最大备份序号（9 → 保留 .1.bak … .9.bak）
   */
  RotatingFileLog(std::string file_path, std::size_t max_bytes = 8 * 1024 * 1024,
                  int max_backup_index = 9);

  void Append(const std::string& line);

  /** 供 alarms/MES 等复用同一滚动策略。 */
  static void RotateBySize(const std::string& file_path, std::size_t max_bytes,
                           int max_backup_index = 9);

 private:
  void RotateIfNeededUnlocked();

  std::string file_path_;
  std::size_t max_bytes_;
  int max_backup_index_;
  std::mutex mutex_;
};

}  // namespace visual

/**
 * @file algo_log.h
 * @brief 算法进程日志：info / debug / warning + 时间戳前缀。
 */
#pragma once

#include <iostream>
#include <string>

#include "visual/log_format.h"

namespace algo {

enum class LogLevel { kInfo = 0, kDebug = 1 };

inline LogLevel& AlgoLogLevelRef() {
  static LogLevel level = LogLevel::kInfo;
  return level;
}

inline void SetAlgoLogLevel(LogLevel level) { AlgoLogLevelRef() = level; }

inline LogLevel GetAlgoLogLevel() { return AlgoLogLevelRef(); }

inline LogLevel ParseLogLevel(const std::string& text) {
  if (text == "debug" || text == "DEBUG" || text == "Debug") {
    return LogLevel::kDebug;
  }
  return LogLevel::kInfo;
}

inline void AlgoInfo(const std::string& msg) {
  std::cout << visual::FormatLogLine(visual::LogSeverity::kInfo, msg) << std::endl;
}

inline void AlgoDebug(const std::string& msg) {
  if (AlgoLogLevelRef() >= LogLevel::kDebug) {
    std::cout << visual::FormatLogLine(visual::LogSeverity::kDebug, msg) << std::endl;
  }
}

inline void AlgoWarn(const std::string& msg) {
  std::cerr << visual::FormatLogLine(visual::LogSeverity::kWarning, msg) << std::endl;
}

inline void AlgoError(const std::string& msg) {
  std::cerr << visual::FormatLogLine(visual::LogSeverity::kWarning, msg) << std::endl;
}

}  // namespace algo

/**
 * @file log_format.h
 * @brief 统一日志前缀：[level] [yyyy-MM-dd hh:mm:ss.zzz] message
 */
#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace visual {

enum class LogSeverity {
  kInfo = 0,
  kDebug = 1,
  kWarning = 2,
};

inline const char* LogSeverityTag(LogSeverity level) {
  switch (level) {
    case LogSeverity::kDebug:
      return "debug";
    case LogSeverity::kWarning:
      return "warning";
    case LogSeverity::kInfo:
    default:
      return "info";
  }
}

/** 生成带级别与毫秒时间戳的完整一行（不含末尾换行）。 */
inline std::string FormatLogLine(LogSeverity level, const std::string& message) {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto t = clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char ts[32]{};
  std::snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
  std::string out;
  out.reserve(message.size() + 48);
  out += '[';
  out += LogSeverityTag(level);
  out += "] [";
  out += ts;
  out += "] ";
  out += message;
  return out;
}

inline void LogToStderr(LogSeverity level, const std::string& message) {
  const std::string line = FormatLogLine(level, message);
  std::fprintf(stderr, "%s\n", line.c_str());
  std::fflush(stderr);
}

}  // namespace visual

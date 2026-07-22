/**
 * @file file_mes_reporter.h
 * @brief 本地文件 MES 上报实现（可替换为真实 MES 客户端）。
 */
#pragma once

#include <mutex>
#include <string>

#include "visual/i_mes_reporter.h"

namespace visual {

class FileMesReporter final : public IMesReporter {
 public:
  explicit FileMesReporter(std::string file_path = "./logs/mes_report.jsonl");

  void ReportCycle(const std::string& vin, StationId station, const LogResultBatch& logs) override;
  void ReportFault(const std::string& subsystem, const std::string& message);

 private:
  std::string file_path_;
  std::mutex mutex_;
};

}  // namespace visual

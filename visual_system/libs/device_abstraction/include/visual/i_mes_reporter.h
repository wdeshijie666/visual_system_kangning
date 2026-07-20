/**
 * @file i_mes_reporter.h
 * @brief MES 上报预留接口（当前 no-op）。
 */
#pragma once

#include <string>

#include "visual/station_types.h"

namespace visual {

class IMesReporter {
 public:
  virtual ~IMesReporter() = default;
  virtual void ReportCycle(const std::string& vin, StationId station, const LogResultBatch& logs) = 0;
};

class NullMesReporter final : public IMesReporter {
 public:
  void ReportCycle(const std::string&, StationId, const LogResultBatch&) override {}
};

}  // namespace visual

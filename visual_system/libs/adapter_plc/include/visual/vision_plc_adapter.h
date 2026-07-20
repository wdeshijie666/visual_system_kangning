/**
 * @file vision_plc_adapter.h
 * @brief 封装 AB_PLC vision_plc_driver。
 */
#pragma once

#include <memory>

#include "visual/i_plc_client.h"

namespace visual {

class VisionPlcAdapter final : public IPlcClient {
 public:
  VisionPlcAdapter();
  ~VisionPlcAdapter() override;

  bool Connect(const PlcConnectionOptions& opts) override;
  void Disconnect() override;
  bool IsConnected() const override;

  bool StartHeartbeat(int interval_ms) override;
  void StopHeartbeat() override;
  bool PollTrigger(StationId station, bool* active) override;
  bool WriteLogResults(StationId station, const LogResultBatch& batch) override;
  bool WriteSequenceCompleted(StationId station, bool completed) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace visual

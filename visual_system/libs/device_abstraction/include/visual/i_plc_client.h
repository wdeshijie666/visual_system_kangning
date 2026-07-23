/**
 * @file i_plc_client.h
 * @brief PLC 交互抽象（底层可换 AB / 仿真实现）。
 */
#pragma once

#include <functional>
#include <string>

#include "visual/station_types.h"

namespace visual {

struct PlcConnectionOptions {
  std::string gateway;
  std::string path = "1,0";
  int timeout_ms = 5000;
  std::string tag_camera_to_plc = "CameraToPLC_Int";
  std::string tag_plc_to_camera = "PLCToCamera_Int";
  /**
   * 仅 MemoryTransport 有效：产线 Start 后按间隔注入 PLC 触发边沿。
   * 真机 libplctag 忽略。
   */
  bool sim_auto_trigger = true;
  /** 循环触发间隔（秒），默认 30。 */
  int sim_auto_trigger_interval_sec = 30;
};

struct PlcTriggerEvent {
  StationId station = StationId::kR05;
  bool active = false;
};

class IPlcClient {
 public:
  virtual ~IPlcClient() = default;

  virtual bool Connect(const PlcConnectionOptions& opts) = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  virtual bool StartHeartbeat(int interval_ms) = 0;
  virtual void StopHeartbeat() = 0;

  /** 轮询指定工位触发；active=true 表示本次有新触发。 */
  virtual bool PollTrigger(StationId station, bool* active) = 0;

  virtual bool WriteLogResults(StationId station, const LogResultBatch& batch) = 0;
  virtual bool WriteSequenceCompleted(StationId station, bool completed) = 0;
};

}  // namespace visual

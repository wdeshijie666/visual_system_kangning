/**
 * @file vision_plc_adapter.cpp
 * @brief AB PLC 适配层，封装 vision_plc_driver。
 *
 * 阶段 2：WorkerLoop 通过 PollTrigger 读 PLCToCamera 触发位
 * 阶段 6.6：RunCycle 通过 WriteLogResults / WriteSequenceCompleted 写回 PLC
 * 详见 docs/框架流程通路.md §5.1、§6.6
 */
#include "visual/vision_plc_adapter.h"

#include "vision_plc/plc_transport.h"
#include "vision_plc/vision_plc_driver.h"
#include "vision_plc/vision_types.h"

namespace visual {
namespace {

vision_plc::VisionStation ToPlcStation(StationId s) {
  switch (s) {
    case StationId::kR05:
      return vision_plc::VisionStation::kR05;
    case StationId::kR07:
      return vision_plc::VisionStation::kR07;
    case StationId::kR09:
      return vision_plc::VisionStation::kR09;
    default:
      return vision_plc::VisionStation::kR05;
  }
}

vision_plc::VisionLogResultBatch ToPlcBatch(const LogResultBatch& batch) {
  vision_plc::VisionLogResultBatch out{};
  for (std::size_t i = 0; i < batch.size(); ++i) {
    out[i].status = static_cast<vision_plc::VisionInspectStatus>(batch[i].status);
    out[i].offset_x_mm = batch[i].offset_x_mm;
    out[i].offset_y_mm = batch[i].offset_y_mm;
    out[i].offset_r_deg = batch[i].offset_r_deg;
    out[i].diameter_mm = batch[i].diameter_mm;
    out[i].length_mm = batch[i].length_mm;
  }
  return out;
}

}  // namespace

struct VisionPlcAdapter::Impl {
  vision_plc::VisionPlcDriver driver;
};

VisionPlcAdapter::VisionPlcAdapter() : impl_(std::make_unique<Impl>()) {
#ifdef VISION_PLC_HAS_LIBPLCTAG
  impl_->driver.SetTransport(vision_plc::CreateLibPlcTagTransport());
#else
  impl_->driver.SetTransport(vision_plc::CreateMemoryTransport());
#endif
}

VisionPlcAdapter::~VisionPlcAdapter() {
  impl_->driver.StopHeartbeat();
  impl_->driver.Disconnect();
}

bool VisionPlcAdapter::Connect(const PlcConnectionOptions& opts) {
  vision_plc::VisionPlcTagConfig tags;
  tags.camera_to_plc = opts.tag_camera_to_plc;
  tags.plc_to_camera = opts.tag_plc_to_camera;
  impl_->driver.SetTagConfig(tags);

  vision_plc::PlcConnectionConfig conn;
  conn.gateway = opts.gateway;
  conn.path = opts.path;
  conn.timeout_ms = opts.timeout_ms;
  return impl_->driver.Connect(conn).ok();
}

void VisionPlcAdapter::Disconnect() {
  impl_->driver.StopHeartbeat();
  impl_->driver.Disconnect();
}

bool VisionPlcAdapter::IsConnected() const {
  return impl_->driver.IsConnected();
}

bool VisionPlcAdapter::StartHeartbeat(int interval_ms) {
  return impl_->driver.StartHeartbeat(interval_ms).ok();
}

void VisionPlcAdapter::StopHeartbeat() {
  impl_->driver.StopHeartbeat();
}

bool VisionPlcAdapter::PollTrigger(StationId station, bool* active) {
  if (active == nullptr) {
    return false;
  }
  vision_plc::VisionTriggerCommand cmd;
  if (!impl_->driver.PollTrigger(ToPlcStation(station), &cmd).ok()) {
    return false;
  }
  *active = cmd.active;
  return true;
}

bool VisionPlcAdapter::WriteLogResults(StationId station, const LogResultBatch& batch) {
  return impl_->driver.WriteLogResults(ToPlcStation(station), ToPlcBatch(batch)).ok();
}

bool VisionPlcAdapter::WriteSequenceCompleted(StationId station, bool completed) {
  return impl_->driver.WriteSequenceCompleted(ToPlcStation(station), completed).ok();
}

}  // namespace visual

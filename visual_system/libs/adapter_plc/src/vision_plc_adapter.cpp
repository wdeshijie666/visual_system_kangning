/**
 * @file vision_plc_adapter.cpp
 * @brief AB PLC 适配层，封装 vision_plc_driver。
 *
 * 双工位并行时 Poll 与两路写回可能并发，入口统一加锁。
 * 无 libplctag 时使用 MemoryTransport，并可循环注入触发、打印写回结果。
 */
#include "visual/vision_plc_adapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>

#include "vision_plc/plc_transport.h"
#include "vision_plc/vision_plc_driver.h"
#include "vision_plc/vision_types.h"
#include "visual/log_format.h"

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

const char* StationTag(StationId s) {
  switch (s) {
    case StationId::kR05:
      return "R05";
    case StationId::kR07:
      return "R07";
    case StationId::kR09:
      return "R09";
    default:
      return "?";
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

void PrintVisionResults(StationId station, const LogResultBatch& batch) {
  {
    std::ostringstream oss;
    oss << "PLC仿真 收到视觉结果 station=" << StationTag(station);
    LogToStderr(LogSeverity::kInfo, oss.str());
  }
  for (std::size_t i = 0; i < batch.size(); ++i) {
    const auto& r = batch[i];
    std::ostringstream oss;
    oss << "PLC仿真 Log" << (i + 1) << " status=" << static_cast<int>(r.status)
        << " X=" << r.offset_x_mm << " Y=" << r.offset_y_mm << " R=" << r.offset_r_deg
        << " D=" << r.diameter_mm << " L=" << r.length_mm;
    LogToStderr(LogSeverity::kInfo, oss.str());
  }
}

}  // namespace

struct VisionPlcAdapter::Impl {
  vision_plc::VisionPlcDriver driver;
  mutable std::mutex io_mutex;
  bool use_memory_transport = false;
  bool sim_auto_trigger = true;
  int sim_auto_trigger_interval_sec = 30;
  std::atomic<bool> inject_running{false};
  std::thread inject_thread;

  void StopInjectUnlocked() {
    inject_running.store(false);
    // 不可在持有 io_mutex 时 join（注入线程会抢同一把锁）
  }

  void JoinInjectThread() {
    if (inject_thread.joinable()) {
      inject_thread.join();
    }
  }

  void StartInjectIfNeeded() {
#ifndef VISION_PLC_HAS_LIBPLCTAG
    if (!use_memory_transport || !sim_auto_trigger) {
      return;
    }
    if (inject_running.exchange(true)) {
      return;
    }
    const int interval_sec =
        sim_auto_trigger_interval_sec > 0 ? sim_auto_trigger_interval_sec : 30;
    inject_thread = std::thread([this, interval_sec]() {
      // 0=R05, 1=R09 交替，避免同 tick 双工位同时压测打满队列
      int phase = 0;
      bool first = true;
      while (inject_running.load()) {
        const int wait_sec = first ? 1 : interval_sec;
        first = false;
        for (int i = 0; i < wait_sec * 10 && inject_running.load(); ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!inject_running.load()) {
          break;
        }
        const auto station =
            (phase % 2 == 0) ? vision_plc::VisionStation::kR05 : vision_plc::VisionStation::kR09;
        const char* name = (phase % 2 == 0) ? "R05" : "R09";
        ++phase;
        {
          std::lock_guard<std::mutex> lock(io_mutex);
          if (!driver.IsConnected()) {
            continue;
          }
          const auto st = driver.SimulatePlcTrigger(station, true);
          if (st.ok()) {
            std::ostringstream oss;
            oss << "PLC仿真 注入触发 " << name << "（间隔 " << interval_sec << "s）";
            LogToStderr(LogSeverity::kInfo, oss.str());
          } else {
            std::ostringstream oss;
            oss << "PLC仿真 注入触发失败 " << name << ": " << st.message;
            LogToStderr(LogSeverity::kWarning, oss.str());
          }
        }
      }
    });
#else
    (void)0;
#endif
  }
};

VisionPlcAdapter::VisionPlcAdapter() : impl_(std::make_unique<Impl>()) {
#ifdef VISION_PLC_HAS_LIBPLCTAG
  impl_->driver.SetTransport(vision_plc::CreateLibPlcTagTransport());
  impl_->use_memory_transport = false;
#else
  impl_->driver.SetTransport(vision_plc::CreateMemoryTransport());
  impl_->use_memory_transport = true;
#endif
}

VisionPlcAdapter::~VisionPlcAdapter() {
  impl_->inject_running.store(false);
  impl_->JoinInjectThread();
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  impl_->driver.StopHeartbeat();
  impl_->driver.Disconnect();
}

bool VisionPlcAdapter::Connect(const PlcConnectionOptions& opts) {
  impl_->inject_running.store(false);
  impl_->JoinInjectThread();

  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  impl_->sim_auto_trigger = opts.sim_auto_trigger;
  impl_->sim_auto_trigger_interval_sec = opts.sim_auto_trigger_interval_sec;

  vision_plc::VisionPlcTagConfig tags;
  tags.camera_to_plc = opts.tag_camera_to_plc;
  tags.plc_to_camera = opts.tag_plc_to_camera;
  impl_->driver.SetTagConfig(tags);

  vision_plc::PlcConnectionConfig conn;
  conn.gateway = opts.gateway;
  conn.path = opts.path;
  conn.timeout_ms = opts.timeout_ms;
  if (!impl_->driver.Connect(conn).ok()) {
    return false;
  }

  // 连接真值：读一次触发位，失败则视为未连上
  vision_plc::VisionTriggerCommand cmd;
  if (!impl_->driver.PollTrigger(vision_plc::VisionStation::kR05, &cmd).ok()) {
    impl_->driver.Disconnect();
    return false;
  }
  return true;
}

void VisionPlcAdapter::Disconnect() {
  impl_->inject_running.store(false);
  impl_->JoinInjectThread();
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  impl_->driver.StopHeartbeat();
  impl_->driver.Disconnect();
}

bool VisionPlcAdapter::IsConnected() const {
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  return impl_->driver.IsConnected();
}

bool VisionPlcAdapter::StartHeartbeat(int interval_ms) {
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    ok = impl_->driver.StartHeartbeat(interval_ms).ok();
  }
  if (ok) {
    impl_->StartInjectIfNeeded();
  }
  return ok;
}

void VisionPlcAdapter::StopHeartbeat() {
  impl_->inject_running.store(false);
  impl_->JoinInjectThread();
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  impl_->driver.StopHeartbeat();
}

bool VisionPlcAdapter::PollTrigger(StationId station, bool* active) {
  if (active == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  vision_plc::VisionTriggerCommand cmd;
  const auto st = impl_->driver.PollTrigger(ToPlcStation(station), &cmd);
  if (!st.ok()) {
    return false;
  }
  *active = cmd.active;
  return true;
}

bool VisionPlcAdapter::WriteLogResults(StationId station, const LogResultBatch& batch) {
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  const bool ok =
      impl_->driver.WriteLogResults(ToPlcStation(station), ToPlcBatch(batch)).ok();
  if (ok && impl_->use_memory_transport) {
    PrintVisionResults(station, batch);
  }
  return ok;
}

bool VisionPlcAdapter::WriteSequenceCompleted(StationId station, bool completed) {
  std::lock_guard<std::mutex> lock(impl_->io_mutex);
  if (impl_->use_memory_transport) {
    std::ostringstream oss;
    oss << "PLC仿真 SequenceCompleted station=" << StationTag(station)
        << " completed=" << (completed ? 1 : 0);
    LogToStderr(LogSeverity::kInfo, oss.str());
  }
  return impl_->driver.WriteSequenceCompleted(ToPlcStation(station), completed).ok();
}

}  // namespace visual

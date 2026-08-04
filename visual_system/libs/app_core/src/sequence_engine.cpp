/**
 * @file sequence_engine.cpp
 * @brief 产线编排实现。流程说明见 docs/框架流程通路.md。
 * 掉线快应答 / 工位门禁 / 异步重连见 sequence_engine.h。
 */
#include "visual/sequence_engine.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <QStringList>

#include "visual/alarm_service.h"
#include "visual/capture_data_format.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"
#include "visual/log_format.h"
#include "visual/shm_algo_service.h"

namespace visual {
namespace {

void FillPreviewFromGray(CycleResultEvent* ev, const GrayImageBuffer& gray) {
  if (ev == nullptr || gray.width == 0 || gray.height == 0 ||
      gray.data.size() < static_cast<std::size_t>(gray.width) * gray.height) {
    return;
  }
  ev->image_width = static_cast<int>(gray.width);
  ev->image_height = static_cast<int>(gray.height);
  ev->image_format = ImagePixelFormat::kMono8;
  ev->image_bytes =
      QByteArray(reinterpret_cast<const char*>(gray.data.data()),
                 static_cast<int>(static_cast<std::size_t>(gray.width) * gray.height));
}

void FillPreviewFromAlgo(CycleResultEvent* ev, const AlgoResponse& resp) {
  if (ev == nullptr || resp.result_image.empty() || resp.result_image_width == 0 ||
      resp.result_image_height == 0 || resp.result_image_format == ImagePixelFormat::kNone) {
    return;
  }
  const std::uint32_t bpp = BytesPerImagePixel(resp.result_image_format);
  const std::size_t need =
      static_cast<std::size_t>(resp.result_image_width) * resp.result_image_height * bpp;
  if (bpp == 0 || resp.result_image.size() < need) {
    return;
  }
  ev->image_width = static_cast<int>(resp.result_image_width);
  ev->image_height = static_cast<int>(resp.result_image_height);
  ev->image_format = resp.result_image_format;
  ev->image_bytes = QByteArray(reinterpret_cast<const char*>(resp.result_image.data()),
                               static_cast<int>(need));
}

std::vector<StationId> CompletedStations(StationId primary, bool single_station_only) {
  if (single_station_only) {
    // 手动单通路：只回报触发工位本身，避免 R05 连带写 R07
    if (primary == StationId::kR09) {
      return {StationId::kR09};
    }
    if (primary == StationId::kR07) {
      return {StationId::kR07};
    }
    return {StationId::kR05};
  }
  if (primary == StationId::kR09) {
    return {StationId::kR09};
  }
  return {StationId::kR05, StationId::kR07};
}

LogResultBatch MakeAllNgLogs() {
  LogResultBatch logs{};
  for (auto& log : logs) {
    log.status = InspectStatus::kNg;
  }
  return logs;
}

}  // namespace

SequenceEngine::SequenceEngine() = default;

SequenceEngine::~SequenceEngine() {
  Stop();
  StopDeviceHealthMonitor();
  DisconnectAllCameras();
}

void SequenceEngine::SetPlcClient(std::shared_ptr<IPlcClient> plc) {
  plc_ = std::move(plc);
}

void SequenceEngine::SetAlgoService(std::shared_ptr<IAlgoService> algo) {
  algo_ = std::move(algo);
  algo_pool_.reset();
}

void SequenceEngine::SetAlgoPool(std::shared_ptr<ShmAlgoServicePool> pool) {
  algo_pool_ = std::move(pool);
  algo_.reset();
}

void SequenceEngine::SetMesReporter(std::shared_ptr<IMesReporter> mes) {
  mes_ = std::move(mes);
}

IAlgoService* SequenceEngine::ResolveAlgo(StationId station) {
  if (algo_pool_) {
    return algo_pool_->TryForStation(station);
  }
  return algo_.get();
}

std::mutex& SequenceEngine::CycleMutexFor(StationId station) {
  return shm::ToShmChannel(station) == shm::ShmChannelId::kR09 ? cycle_mutex_r09_ : cycle_mutex_r05_;
}

CycleJobQueue<SequenceEngine::PendingCycle>& SequenceEngine::CycleQueueFor(StationId station) {
  return shm::ToShmChannel(station) == shm::ShmChannelId::kR09 ? cycle_queue_r09_ : cycle_queue_r05_;
}

FaultBreaker& SequenceEngine::CaptureBreakerFor(StationId station) {
  return shm::ToShmChannel(station) == shm::ShmChannelId::kR09 ? capture_breaker_r09_
                                                               : capture_breaker_r05_;
}

bool SequenceEngine::IsStationCameraReady(StationId station) const {
  return shm::ToShmChannel(station) == shm::ShmChannelId::kR09 ? camera_ready_r09_.load()
                                                               : camera_ready_r05_.load();
}

void SequenceEngine::SetStationCameraReady(StationId station, bool ready) {
  if (shm::ToShmChannel(station) == shm::ShmChannelId::kR09) {
    camera_ready_r09_.store(ready);
  } else {
    camera_ready_r05_.store(ready);
  }
}

void SequenceEngine::NotifyQueuesStop() {
  cycle_queue_r05_.NotifyAll();
  cycle_queue_r09_.NotifyAll();
}

void SequenceEngine::RegisterCamera(const std::string& id, CameraPtr camera) {
  const bool connected = camera && camera->IsConnected();
  cameras_[id] = std::move(camera);

  // 按 devices.json 工位初始化门禁，避免启动后短窗误入队
  const auto& devices = AppContext::Instance().Devices();
  auto dit = devices.find(id);
  StationId sid = StationId::kR05;
  if (dit != devices.end() &&
      (dit->second.station == "r09" || dit->second.station == "R09")) {
    sid = StationId::kR09;
  }
  if (!connected) {
    SetStationCameraReady(sid, false);
  }
  StartDeviceHealthMonitor();
}

CameraPtr SequenceEngine::GetCamera(const std::string& id) const {
  const auto it = cameras_.find(id);
  if (it == cameras_.end()) {
    return nullptr;
  }
  return it->second;
}

void SequenceEngine::DisconnectAllCameras() {
  StopDeviceHealthMonitor();
  for (auto& kv : cameras_) {
    if (kv.second) {
      kv.second->Disconnect();
      EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.first), false);
    }
  }
  cameras_.clear();
  camera_ready_r05_.store(false);
  camera_ready_r09_.store(false);
  std::lock_guard<std::mutex> lk(reconnect_mu_);
  reconnect_state_.clear();
}

bool SequenceEngine::TryConnectPlc() {
  if (!plc_) {
    EventBus::Instance().NotifyPlcStatus(false, false);
    return false;
  }
  if (plc_->IsConnected()) {
    EventBus::Instance().NotifyPlcStatus(true, true);
    return true;
  }
  const auto& opts = AppContext::Instance().Settings().plc;
  if (opts.gateway.empty()) {
    EventBus::Instance().NotifyPlcStatus(false, false);
    return false;
  }
  const bool ok = plc_->Connect(opts);
  EventBus::Instance().NotifyPlcStatus(ok, ok);
  if (!ok) {
    AlarmService::Instance().Raise(AlarmLevel::kCritical, QStringLiteral("PLC"),
                                   QStringLiteral("PLC 连接失败"));
  }
  return ok;
}

bool SequenceEngine::IsPlcConnected() const {
  return plc_ && plc_->IsConnected();
}

void SequenceEngine::ResetFaultBreakers() {
  capture_breaker_r05_.Reset();
  capture_breaker_r09_.Reset();
  algo_breaker_.Reset();
  plc_breaker_.Reset();
  EventBus::Instance().NotifyLog(QStringLiteral("故障熔断已复位（相机门禁仍由探活/重连维护）"));
}

bool SequenceEngine::FastAckPlc(StationId station, const char* reason) {
  const QString why = QString::fromUtf8(reason ? reason : "相机离线");
  const QString st_name =
      station == StationId::kR09 ? QStringLiteral("R09") : QStringLiteral("R05");

  AlarmService::Instance().Raise(AlarmLevel::kWarning, QStringLiteral("Camera"),
                                 QStringLiteral("工位%1 %2，已快应答 PLC（全 NG + 完成位）")
                                     .arg(st_name)
                                     .arg(why));
  EventBus::Instance().NotifyLog(
      LogSeverity::kWarning,
      QStringLiteral("快应答 PLC 工位=%1 原因=%2（不跑采图/算法）").arg(st_name).arg(why));

  EventBus::Instance().NotifyCycleStarted(station);
  const LogResultBatch ng_logs = MakeAllNgLogs();
  bool plc_ok = true;
  if (plc_) {
    for (StationId target : CompletedStations(station, /*single_station_only=*/false)) {
      if (!plc_->WriteLogResults(target, ng_logs)) {
        plc_ok = false;
      }
      if (!plc_->WriteSequenceCompleted(target, true)) {
        plc_ok = false;
      }
    }
  } else {
    plc_ok = false;
  }

  CycleResultEvent ev;
  ev.station = station;
  ev.logs = ng_logs;
  ev.plc_ok = plc_ok;
  ev.algo_ok = false;
  EventBus::Instance().NotifyCycleCompleted(ev);
  return plc_ok;
}

void SequenceEngine::HandleProductionTrigger(StationId station, const StationConfig& cfg) {
  if (!cfg.enabled) {
    return;
  }
  // 门禁 Offline / 该通道采图已隔离：直接回完成，不入队
  if (!IsStationCameraReady(station) || CaptureBreakerFor(station).IsTripped()) {
    if (CaptureBreakerFor(station).IsTripped() && IsStationCameraReady(station)) {
      SetStationCameraReady(station, false);
    }
    FastAckPlc(station, CaptureBreakerFor(station).IsTripped() ? "采图通道已隔离"
                                                               : "相机门禁 Offline");
    EventBus::Instance().NotifyTrigger(station);
    return;
  }

  PendingCycle job;
  job.station = station;
  job.cfg = cfg;
  job.options.capture_live = true;
  job.options.write_plc = true;
  if (!CycleQueueFor(station).Push(std::move(job))) {
    // 队列满时仍回完成，避免 PLC 死等
    AlarmService::Instance().Raise(
        AlarmLevel::kWarning, QStringLiteral("Engine"),
        QStringLiteral("周期队列已满，改为快应答 station=%1").arg(static_cast<int>(station)));
    EventBus::Instance().NotifyLog(
        LogSeverity::kWarning,
        QStringLiteral("周期队列已满，快应答 PLC（请放慢节拍）"));
    FastAckPlc(station, "周期队列已满");
    EventBus::Instance().NotifyTrigger(station);
  } else {
    EventBus::Instance().NotifyTrigger(station);
  }
}

bool SequenceEngine::Start() {
  // 熔断只清 running_ 不 join：此处先回收残留线程，再拉起新 worker
  if (poll_thread_.joinable() || cycle_thread_r05_.joinable() || cycle_thread_r09_.joinable()) {
    running_.store(false);
    NotifyQueuesStop();
    JoinWorkerThreads();
  }

  if (running_.exchange(true)) {
    return true;
  }
  CaptureSaveWorker::Instance().Start();
  TryConnectPlc();
  if (plc_) {
    plc_->StartHeartbeat(2000);
  }
  if (algo_pool_) {
    algo_pool_->Start();
  } else if (algo_) {
    algo_->Start();
  }
  ResetFaultBreakers();
  cycle_queue_r05_.Clear();
  cycle_queue_r09_.Clear();
  cycle_thread_r05_ = std::thread([this]() { CycleWorkerLoopR05(); });
  cycle_thread_r09_ = std::thread([this]() { CycleWorkerLoopR09(); });
  poll_thread_ = std::thread([this]() { PollLoop(); });
  return true;
}

void SequenceEngine::JoinWorkerThreads() {
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
  if (cycle_thread_r05_.joinable()) {
    cycle_thread_r05_.join();
  }
  if (cycle_thread_r09_.joinable()) {
    cycle_thread_r09_.join();
  }
}

void SequenceEngine::Stop() {
  // 不论用 exchange 短路：熔断后 running_ 已是 false，仍须 join
  running_.store(false);
  NotifyQueuesStop();
  JoinWorkerThreads();
  cycle_queue_r05_.Clear();
  cycle_queue_r09_.Clear();
  if (plc_) {
    plc_->StopHeartbeat();
  }
  if (algo_pool_) {
    algo_pool_->Stop();
  } else if (algo_) {
    algo_->Stop();
  }
  CaptureSaveWorker::Instance().Stop();
}

bool SequenceEngine::IsRunning() const {
  return running_.load();
}

bool SequenceEngine::RunOfflineCycle(StationId station) {
  if (IsRunning()) {
    EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("请先停止产线，再执行离线/手动周期"));
    return false;
  }
  const auto& settings = AppContext::Instance().Settings();
  StationConfig cfg = (station == StationId::kR09) ? settings.station_r09 : settings.station_r05;
  CycleOptions options;
  options.capture_live = true;
  options.write_plc = true;
  // 手动触发：单通路隔离，不因其他工位/熔断状态互相干扰
  options.single_station_only = true;
  options.update_fault_breaker = false;
  EventBus::Instance().NotifyLog(
      QStringLiteral("手动触发开始 工位=%1")
          .arg(station == StationId::kR09 ? QStringLiteral("R09") : QStringLiteral("R05")));
  return RunCycle(station, cfg, options);
}

bool SequenceEngine::RunReplayCycle(StationId station, const std::string& session_dir) {
  if (IsRunning()) {
    EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("请先停止产线，再执行回放"));
    return false;
  }
  if (session_dir.empty() || !SessionDirHasCaptureData(session_dir)) {
    EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("回放失败：会话目录无效或无采图数据"));
    return false;
  }
  const auto& settings = AppContext::Instance().Settings();
  StationConfig cfg = (station == StationId::kR09) ? settings.station_r09 : settings.station_r05;
  CycleOptions options;
  options.capture_live = false;
  options.write_plc = false;
  options.replay_session_dir = session_dir;
  options.algo_result_suffix = "replay_algo_result";
  return RunCycle(station, cfg, options);
}

void SequenceEngine::StartDeviceHealthMonitor() {
  bool expected = false;
  if (!health_monitor_enabled_.compare_exchange_strong(expected, true)) {
    return;
  }
  health_thread_ = std::thread([this]() { DeviceHealthLoop(); });
}

void SequenceEngine::StopDeviceHealthMonitor() {
  health_monitor_enabled_.store(false);
  if (health_thread_.joinable()) {
    health_thread_.join();
  }
}

void SequenceEngine::DeviceHealthLoop() {
  while (health_monitor_enabled_.load()) {
    CheckDeviceHealth();
    // 2s 周期，切片 sleep 便于退出时快速 join
    for (int i = 0; i < 40 && health_monitor_enabled_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
}

void SequenceEngine::CheckDeviceHealth() {
  std::lock_guard<std::mutex> health_lock(health_mutex_);
  if (!health_monitor_enabled_.load()) {
    return;
  }

  const auto& settings = AppContext::Instance().Settings();
  const auto& devices = AppContext::Instance().Devices();
  const auto now = std::chrono::steady_clock::now();

  for (auto& kv : cameras_) {
    if (!kv.second) {
      continue;
    }
    StationId sid = StationId::kR05;
    auto dit = devices.find(kv.first);
    if (dit != devices.end()) {
      sid = (dit->second.station == "r09" || dit->second.station == "R09") ? StationId::kR09
                                                                          : StationId::kR05;
      if (!IsStationEnabled(settings, sid)) {
        continue;
      }
    }

    // 仅探活：不抢 cycle_mutex，避免与周期互相堵死
    const CameraProbeResult probe = kv.second->ProbeAlive();
    if (probe == CameraProbeResult::kBusy) {
      continue;
    }
    if (probe == CameraProbeResult::kAlive) {
      EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.first), true);
      SetStationCameraReady(sid, true);
      CaptureBreakerFor(sid).Reset();
      {
        std::lock_guard<std::mutex> lk(reconnect_mu_);
        reconnect_state_[kv.first] = CamReconnectState{};
      }
      continue;
    }

    // 已确认掉线：立刻门禁 Offline，后续 PLC 触发走快应答
    EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.first), false);
    SetStationCameraReady(sid, false);

    CamReconnectState st;
    {
      std::lock_guard<std::mutex> lk(reconnect_mu_);
      st = reconnect_state_[kv.first];
    }
    if (now < st.next_attempt) {
      continue;
    }

    EventBus::Instance().NotifyLog(
        QStringLiteral("相机探活掉线，异步重连: %1").arg(QString::fromStdString(kv.first)));
    // 不持有 cycle_mutex：Connect 阻塞时健康工位仍可跑周期
    kv.second->Disconnect();
    const bool ok = kv.second->Connect();
    EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.first), ok);
    if (ok) {
      SetStationCameraReady(sid, true);
      CaptureBreakerFor(sid).Reset();
      {
        std::lock_guard<std::mutex> lk(reconnect_mu_);
        reconnect_state_[kv.first] = CamReconnectState{};
      }
      EventBus::Instance().NotifyLog(
          QStringLiteral("相机已重连: %1").arg(QString::fromStdString(kv.first)));
    } else {
      AlarmService::Instance().Raise(
          AlarmLevel::kCritical, QStringLiteral("Camera"),
          QStringLiteral("相机掉线且重连失败: %1").arg(QString::fromStdString(kv.first)));
      const int streak = st.fail_streak + 1;
      // 退避：2s、4s、8s…封顶 30s，减轻 SDK 刷死
      const int delay_sec = std::min(30, 2 << std::min(streak, 4));
      CamReconnectState next;
      next.fail_streak = streak;
      next.next_attempt = now + std::chrono::seconds(delay_sec);
      std::lock_guard<std::mutex> lk(reconnect_mu_);
      reconnect_state_[kv.first] = next;
    }
  }

  // PLC：断开则重连（产线未 Start 时也可能恢复，不影响相机通路）
  if (plc_ && !plc_->IsConnected()) {
    EventBus::Instance().NotifyPlcStatus(false, false);
    if (!TryConnectPlc()) {
      AlarmService::Instance().Raise(AlarmLevel::kCritical, QStringLiteral("PLC"),
                                     QStringLiteral("PLC 掉线且重连失败"));
    } else if (plc_ && running_.load()) {
      plc_->StopHeartbeat();
      plc_->StartHeartbeat(2000);
    }
  }
}

void SequenceEngine::OnCycleOutcome(StationId station, bool capture_ok, bool algo_ok, bool plc_ok,
                                    const std::string& cycle_id, bool update_fault_breaker) {
  if (!update_fault_breaker) {
    return;
  }
  const QString cid = QString::fromStdString(cycle_id);
  auto& cap_br = CaptureBreakerFor(station);
  if (capture_ok) {
    cap_br.OnSuccess();
    SetStationCameraReady(station, true);
  } else if (cap_br.OnFailure()) {
    // 仅隔离本通道：门禁 Offline，后续快应答；不停整线
    SetStationCameraReady(station, false);
    AlarmService::Instance().Raise(
        AlarmLevel::kCritical, QStringLiteral("Camera"),
        QStringLiteral("采图连续失败，工位通道已隔离（快应答），其它工位继续"), cid);
    EventBus::Instance().NotifyLog(
        LogSeverity::kWarning,
        QStringLiteral("采图通道隔离 station=%1（不停产线）").arg(static_cast<int>(station)));
  } else {
    // 首次/未满阈值失败：立刻 Offline，避免下一拍再进长 Capture
    SetStationCameraReady(station, false);
  }

  if (algo_ok) {
    algo_breaker_.OnSuccess();
  } else if (algo_breaker_.OnFailure()) {
    AlarmService::Instance().Raise(AlarmLevel::kCritical, QStringLiteral("Algo"),
                                   QStringLiteral("算法连续失败，触发熔断"), cid);
    running_.store(false);
    NotifyQueuesStop();
  }

  if (plc_ok) {
    plc_breaker_.OnSuccess();
  } else if (plc_breaker_.OnFailure()) {
    AlarmService::Instance().Raise(AlarmLevel::kCritical, QStringLiteral("PLC"),
                                   QStringLiteral("PLC 写回连续失败，触发熔断"), cid);
    running_.store(false);
    NotifyQueuesStop();
  }
}

void SequenceEngine::PollLoop() {
  bool last_r05 = false;
  // R05/R07 现场合并为一工位且共用触发位；暂屏蔽 R07 监听，恢复时取消本段相关注释即可。
  // bool last_r07 = false;
  bool last_r09 = false;

  while (running_.load()) {
    // 算法/PLC 全局熔断仍停接新触发；相机失败不再停整线
    if (algo_breaker_.IsTripped() || plc_breaker_.IsTripped()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    if (!plc_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    bool r05 = false;
    // bool r07 = false;
    bool r09 = false;
    const bool ok_r05 = plc_->PollTrigger(StationId::kR05, &r05);
    // const bool ok_r07 = plc_->PollTrigger(StationId::kR07, &r07);
    const bool ok_r09 = plc_->PollTrigger(StationId::kR09, &r09);
    if (!ok_r05 || /* !ok_r07 || */ !ok_r09) {
      EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("PLC 触发读取失败"));
      EventBus::Instance().NotifyPlcStatus(false, false);
      AlarmService::Instance().Raise(AlarmLevel::kWarning, QStringLiteral("PLC"),
                                     QStringLiteral("PLC 触发轮询失败，尝试重连"));
      TryConnectPlc();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    const bool edge_r05 = r05 && !last_r05;
    // const bool edge_r07 = r07 && !last_r07;
    const bool edge_r09 = r09 && !last_r09;
    last_r05 = r05;
    // last_r07 = r07;
    last_r09 = r09;

    if (edge_r09) {
      HandleProductionTrigger(StationId::kR09, AppContext::Instance().Settings().station_r09);
    }
    // 原：edge_r05 || edge_r07；R07 监听屏蔽后仅看 R05 边沿。
    if (edge_r05 /* || edge_r07 */) {
      HandleProductionTrigger(StationId::kR05, AppContext::Instance().Settings().station_r05);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void SequenceEngine::CycleWorkerLoopR05() {
  while (running_.load()) {
    auto job = cycle_queue_r05_.PopWait(running_);
    if (!job.has_value()) {
      continue;
    }
    if (!running_.load()) {
      break;
    }
    RunCycle(job->station, job->cfg, job->options);
  }
}

void SequenceEngine::CycleWorkerLoopR09() {
  while (running_.load()) {
    auto job = cycle_queue_r09_.PopWait(running_);
    if (!job.has_value()) {
      continue;
    }
    if (!running_.load()) {
      break;
    }
    RunCycle(job->station, job->cfg, job->options);
  }
}

bool SequenceEngine::RunCycle(StationId station, StationConfig station_cfg, const CycleOptions& options) {
  // 同工位互斥：在线 Worker 与 UI 离线/回放不会同时采同一相机、踩同一 SHM
  std::lock_guard<std::mutex> cycle_lock(CycleMutexFor(station));

  // 入队后才掉线：产线路径改为快应答，避免再进长 Capture
  if (options.capture_live && options.write_plc && !options.single_station_only &&
      !IsStationCameraReady(station)) {
    FastAckPlc(station, "入队后相机门禁 Offline");
    return false;
  }

  EventBus::Instance().NotifyCycleStarted(station);

  const auto& settings = AppContext::Instance().Settings();

  CaptureRecordContext record_ctx =
      options.capture_live ? BuildCaptureRecordContext(station, settings.data_path)
                           : BuildReplayRecordContext(options.replay_session_dir, station);

  const std::string cycle_id = record_ctx.timestamp_prefix;

  AlgoRequest req;
  req.station = station;
  req.session_dir = record_ctx.output_dir;
  req.transfer_flags = BuildAlgoTransferFlags(settings);

  bool capture_ok = true;

  if (options.capture_live) {
    req.input_mode = AlgoInputMode::kOnlineShm;
    int ok_count = 0;
    for (const auto& cam_id : station_cfg.camera_ids) {
      auto it = cameras_.find(cam_id);
      if (it == cameras_.end() || !it->second) {
        EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
            QStringLiteral("相机未配置: %1").arg(QString::fromStdString(cam_id)));
        capture_ok = false;
        continue;
      }
      // 手动单通路：采图前若已探活为掉线，再试一次重连（不占用探活线程时机）
      if (!it->second->IsConnected() && options.single_station_only) {
        EventBus::Instance().NotifyLog(
            QStringLiteral("尝试重连相机: %1").arg(QString::fromStdString(cam_id)));
        it->second->Disconnect();
        const bool re_ok = it->second->Connect();
        EventBus::Instance().NotifyCameraStatus(QString::fromStdString(cam_id), re_ok);
      }
      if (!it->second->IsConnected()) {
        EventBus::Instance().NotifyCameraStatus(QString::fromStdString(cam_id), false);
        capture_ok = false;
        EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
            QStringLiteral("采图跳过，相机离线: %1").arg(QString::fromStdString(cam_id)));
        continue;
      }

      CaptureCopyOptions copy_opts;
      const auto cam_info_pre = it->second->GetInfo();
      // SHM 传输需要则拷；仿真落盘需要堆上深度/点云；实机深度落盘走 SDK 不必拷深度
      copy_opts.copy_depth =
          settings.algo_transfer_depth || (settings.stub_save_depth && cam_info_pre.is_stub);
      copy_opts.copy_pointcloud =
          settings.algo_transfer_pointcloud || settings.stub_save_pointcloud;
      copy_opts.copy_gray = settings.algo_transfer_gray || settings.stub_save_gray;
      CaptureBundle bundle = it->second->Capture(copy_opts);
      const std::string file_prefix = MakeCaptureFilePrefix(record_ctx, cam_id);
      if (!bundle.ok) {
        capture_ok = false;
        EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
            QStringLiteral("采图失败 相机=%1 耗时=%2ms 原因=%3")
                .arg(QString::fromStdString(cam_id))
                .arg(bundle.capture_ms)
                .arg(QString::fromStdString(bundle.error_message.empty() ? "未知"
                                                                         : bundle.error_message)));
        EventBus::Instance().NotifyCameraStatus(QString::fromStdString(cam_id), it->second->IsConnected());
      } else {
        ++ok_count;
        AssignCapturePaths(&bundle, record_ctx.output_dir, file_prefix);

        bool depth_saved = true;
        if (!bundle.depth_path.empty()) {
          depth_saved =
              it->second->SaveLastCaptureToDir(record_ctx.output_dir, file_prefix, &bundle);
          if (!depth_saved) {
            EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
                QStringLiteral("深度图保存失败 相机=%1").arg(QString::fromStdString(cam_id)));
          }
        }

        if (!bundle.pointcloud_path.empty()) {
          CaptureBundle ply_job;
          ply_job.ok = true;
          ply_job.camera_serial = bundle.camera_serial;
          ply_job.pointcloud = bundle.pointcloud;
          ply_job.pointcloud_path = bundle.pointcloud_path;
          CaptureSaveWorker::Instance().Enqueue(std::move(ply_job));
        }

        if (!bundle.gray_path.empty() && bundle.gray) {
          CaptureBundle gray_job;
          gray_job.ok = true;
          gray_job.camera_serial = bundle.camera_serial;
          gray_job.gray = bundle.gray;
          gray_job.gray_path = bundle.gray_path;
          CaptureSaveWorker::Instance().Enqueue(std::move(gray_job));
        }

        const auto cam_info = it->second->GetInfo();
        const QString kind = cam_info.is_stub ? QStringLiteral("仿真") : QStringLiteral("实机");
        QString line = QStringLiteral("采图成功 相机=%1 类型=%2 耗时=%3ms")
                           .arg(QString::fromStdString(cam_id), kind)
                           .arg(bundle.capture_ms);
        if (bundle.depth) {
          line += QStringLiteral(" 深度=%1x%2")
                      .arg(bundle.depth->width)
                      .arg(bundle.depth->height);
        }
        EventBus::Instance().NotifyLog(line);
        if (cam_info.is_stub && !IsSimulationMode(settings)) {
          EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
              QStringLiteral("警告: 实机模式下相机 %1 仍为仿真相机")
                  .arg(QString::fromStdString(cam_id)));
        }
      }
      req.captures.push_back(std::move(bundle));
    }
    if (ok_count == 0 || station_cfg.camera_ids.empty()) {
      capture_ok = false;
    }
  } else {
    req.input_mode = AlgoInputMode::kOfflinePath;
    req.transfer_flags = 0;
    for (const auto& cam_id : station_cfg.camera_ids) {
      CaptureBundle bundle;
      bundle.camera_serial = cam_id;
      bundle.depth_path =
          FindDepthImageInSession(record_ctx.output_dir, record_ctx.station_tag, cam_id);
      bundle.ok = !bundle.depth_path.empty();
      if (!bundle.ok) {
        capture_ok = false;
      }
      req.captures.push_back(bundle);
    }
  }

  // P0-5：采图失败不调算法，直接 NG 写回 PLC；并置门禁 Offline 供后续快应答
  if (options.capture_live && !capture_ok) {
    SetStationCameraReady(station, false);
    AlarmService::Instance().Raise(AlarmLevel::kWarning, QStringLiteral("Camera"),
                                   QStringLiteral("采图失败，本周期跳过算法"),
                                   QString::fromStdString(cycle_id));
    const LogResultBatch ng_logs = MakeAllNgLogs();
    SaveAlgoResultCsv(record_ctx, ng_logs, options.algo_result_suffix);

    bool plc_ok = true;
    if (options.write_plc && plc_) {
      if (options.single_station_only && !plc_->IsConnected()) {
        EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("手动周期: PLC 未连接，跳过 NG 写回"));
      } else {
        for (StationId target : CompletedStations(station, options.single_station_only)) {
          if (!plc_->WriteLogResults(target, ng_logs)) {
            plc_ok = false;
          }
          if (!plc_->WriteSequenceCompleted(target, true)) {
            plc_ok = false;
          }
        }
      }
    }
    if (mes_) {
      mes_->ReportCycle("", station, ng_logs);
      mes_->ReportFault("Camera", "capture failed");
    }

    CycleResultEvent ev;
    ev.station = station;
    ev.logs = ng_logs;
    ev.session_dir = QString::fromStdString(record_ctx.output_dir);
    ev.plc_ok = plc_ok;
    ev.algo_ok = false;
    EventBus::Instance().NotifyCycleCompleted(ev);
    EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
        QStringLiteral("周期中止：采图失败 工位=%1")
            .arg(station == StationId::kR09 ? QStringLiteral("R09") : QStringLiteral("R05")));
    OnCycleOutcome(station, false, false, plc_ok, cycle_id, options.update_fault_breaker);
    return false;
  }

  AlgoResponse algo_resp;
  bool algo_ok = false;
  IAlgoService* algo = ResolveAlgo(station);

  // 请求摘要
  {
    QStringList caps;
    for (std::size_t i = 0; i < req.captures.size(); ++i) {
      const auto& c = req.captures[i];
      if (c.depth) {
        caps << QStringLiteral("%1x%2").arg(c.depth->width).arg(c.depth->height);
      }
    }
    EventBus::Instance().NotifyLog(
        QStringLiteral("算法请求 工位=%1 深度=%2")
            .arg(static_cast<int>(station))
            .arg(caps.isEmpty() ? QStringLiteral("无") : caps.join(QLatin1Char(','))));
  }

  if (algo != nullptr) {
    if (settings.use_shm_algo && !EventBus::IsAlgoProcessReady()) {
      algo_resp.ok = false;
      algo_resp.message = "algo process not ready";
      AlarmService::Instance().Raise(AlarmLevel::kWarning, QStringLiteral("Algo"),
                                     QStringLiteral("算法进程未就绪，本周期跳过"),
                                     QString::fromStdString(cycle_id));
    } else {
      algo_ok = algo->Run(req, &algo_resp, settings.algo_timeout_ms);
      if (!algo_ok && algo_resp.message.find("timeout") != std::string::npos) {
        AlarmService::Instance().Raise(AlarmLevel::kCritical, QStringLiteral("Algo"),
                                       QStringLiteral("算法超时，已复位 SHM，请求重启算法进程"),
                                       QString::fromStdString(cycle_id));
        EventBus::Instance().NotifyRequestAlgoRestart(QString::fromStdString(algo_resp.message));
      }
    }
  }

  if (!algo_ok) {
    algo_resp.logs = MakeAllNgLogs();
  }

  {
    int ok_n = 0;
    int ng_n = 0;
    for (const auto& L : algo_resp.logs) {
      if (L.status == InspectStatus::kOk) {
        ++ok_n;
      } else if (L.status == InspectStatus::kNg) {
        ++ng_n;
      }
    }
    if (algo_ok) {
      EventBus::Instance().NotifyLog(
          QStringLiteral("算法完成 合格=%1 不合格=%2").arg(ok_n).arg(ng_n));
    } else {
      EventBus::Instance().NotifyLog(LogSeverity::kWarning, 
          QStringLiteral("算法失败: %1")
              .arg(QString::fromStdString(algo_resp.message.empty() ? "未知错误"
                                                                    : algo_resp.message)));
    }
  }

  if (!SaveAlgoResultCsv(record_ctx, algo_resp.logs, options.algo_result_suffix)) {
    EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("算法结果保存失败"));
  }

  bool plc_ok = true;
  if (options.write_plc && plc_) {
    if (options.single_station_only && !plc_->IsConnected()) {
      EventBus::Instance().NotifyLog(LogSeverity::kWarning, QStringLiteral("手动周期: PLC 未连接，跳过写回"));
      plc_ok = true;
    } else {
      for (StationId target : CompletedStations(station, options.single_station_only)) {
        if (!plc_->WriteLogResults(target, algo_resp.logs)) {
          plc_ok = false;
        }
        if (!plc_->WriteSequenceCompleted(target, true)) {
          plc_ok = false;
        }
      }
      if (!plc_ok) {
        AlarmService::Instance().Raise(AlarmLevel::kWarning, QStringLiteral("PLC"),
                                       QStringLiteral("PLC 结果写回失败"),
                                       QString::fromStdString(cycle_id));
      }
    }
  }

  if (mes_) {
    mes_->ReportCycle("", station, algo_resp.logs);
  }

  CycleResultEvent ev;
  ev.station = station;
  ev.logs = algo_resp.logs;
  ev.session_dir = QString::fromStdString(record_ctx.output_dir);
  // 优先显示算法回传可视化图；无合格检出时强制本周期采图灰度，避免沿用上一周期标注残图。
  bool any_ok = false;
  for (const auto& L : algo_resp.logs) {
    if (L.status == InspectStatus::kOk) {
      any_ok = true;
      break;
    }
  }
  if (any_ok) {
    FillPreviewFromAlgo(&ev, algo_resp);
  }
  if (ev.image_bytes.isEmpty()) {
    for (const auto& capture : req.captures) {
      if (capture.ok && capture.gray) {
        FillPreviewFromGray(&ev, *capture.gray);
        if (!ev.image_bytes.isEmpty()) {
          break;
        }
      }
    }
  }
  ev.plc_ok = plc_ok;
  ev.algo_ok = algo_ok;
  EventBus::Instance().NotifyCycleCompleted(ev);

  const QString mode = options.capture_live
                           ? (IsSimulationMode(AppContext::Instance().Settings())
                                  ? QStringLiteral("仿真")
                                  : QStringLiteral("实机"))
                           : QStringLiteral("回放");
  EventBus::Instance().NotifyLog(
      QStringLiteral("周期结束 模式=%1 工位=%2 算法=%3 PLC=%4")
          .arg(mode)
          .arg(station == StationId::kR09 ? QStringLiteral("R09") : QStringLiteral("R05"))
          .arg(algo_ok ? QStringLiteral("成功") : QStringLiteral("失败"))
          .arg(options.write_plc ? (plc_ok ? QStringLiteral("成功") : QStringLiteral("失败"))
                                 : QStringLiteral("跳过")));

  OnCycleOutcome(station, capture_ok, algo_ok, options.write_plc ? plc_ok : true, cycle_id,
                 options.update_fault_breaker);
  // 手动单通路：以采图+算法为准，PLC 写回失败不判定整周期失败
  if (options.single_station_only) {
    return capture_ok && algo_ok;
  }
  return algo_ok && (options.write_plc ? plc_ok : true);
}

}  // namespace visual

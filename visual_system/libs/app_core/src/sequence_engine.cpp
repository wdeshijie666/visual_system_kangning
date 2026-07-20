/**
 * @file sequence_engine.cpp
 * @brief 产线编排实现。流程说明见 docs/框架流程通路.md。
 */
#include "visual/sequence_engine.h"

#include <chrono>
#include <thread>

#include <QStringList>

#include "visual/data_recorder.h"
#include "visual/event_bus.h"

namespace visual {
namespace {

void FillGrayPreview(CycleResultEvent* ev, const GrayImageBuffer& gray) {
  if (ev == nullptr || gray.width == 0 || gray.height == 0 ||
      gray.data.size() < static_cast<std::size_t>(gray.width) * gray.height) {
    return;
  }
  ev->gray_width = static_cast<int>(gray.width);
  ev->gray_height = static_cast<int>(gray.height);
  ev->gray_bytes =
      QByteArray(reinterpret_cast<const char*>(gray.data.data()),
                 static_cast<int>(static_cast<std::size_t>(gray.width) * gray.height));
}

/** R09 触发时返回 R09；R05/R07 共用 R05 工位配置。 */
StationId TriggerToStation(bool r05_active, bool r07_active, bool r09_active) {
  if (r09_active) {
    return StationId::kR09;
  }
  if (r05_active || r07_active) {
    return StationId::kR05;
  }
  return StationId::kR05;
}

/**
 * 一次 R05 视觉周期需同时写 R05 + R07 结果区（V0.2 双机械臂映射）。
 * R09 周期只写 R09。
 */
std::vector<StationId> CompletedStations(StationId primary) {
  if (primary == StationId::kR09) {
    return {StationId::kR09};
  }
  return {StationId::kR05, StationId::kR07};
}

}  // namespace

SequenceEngine::SequenceEngine() = default;

SequenceEngine::~SequenceEngine() {
  Stop();
  DisconnectAllCameras();
}

void SequenceEngine::SetPlcClient(std::shared_ptr<IPlcClient> plc) {
  plc_ = std::move(plc);
}

void SequenceEngine::SetAlgoService(std::shared_ptr<IAlgoService> algo) {
  algo_ = std::move(algo);
}

void SequenceEngine::SetMesReporter(std::shared_ptr<IMesReporter> mes) {
  mes_ = std::move(mes);
}

void SequenceEngine::RegisterCamera(const std::string& id, CameraPtr camera) {
  cameras_[id] = std::move(camera);
}

CameraPtr SequenceEngine::GetCamera(const std::string& id) const {
  const auto it = cameras_.find(id);
  if (it == cameras_.end()) {
    return nullptr;
  }
  return it->second;
}

void SequenceEngine::DisconnectAllCameras() {
  for (auto& kv : cameras_) {
    if (kv.second) {
      kv.second->Disconnect();
      EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.first), false);
    }
  }
  cameras_.clear();
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
  return ok;
}

bool SequenceEngine::IsPlcConnected() const {
  return plc_ && plc_->IsConnected();
}

bool SequenceEngine::Start() {
  if (running_.exchange(true)) {
    return true;
  }
  // 采集 depth/ply 异步落盘线程（不阻塞算法与 PLC；由 dataStub 开关控制写哪些）
  CaptureSaveWorker::Instance().Start();
  // PLC 连接与 2s 心跳（CameraToPLC_Int[0].0）
  TryConnectPlc();
  if (plc_) {
    plc_->StartHeartbeat(2000);
  }
  // 映射 SHM v2（与 mock_algo_service 共享同名区域）
  if (algo_) {
    algo_->Start();
  }
  worker_ = std::thread([this]() { WorkerLoop(); });
  return true;
}

void SequenceEngine::Stop() {
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
  if (plc_) {
    plc_->StopHeartbeat();
  }
  if (algo_) {
    algo_->Stop();
  }
  CaptureSaveWorker::Instance().Stop();
}

bool SequenceEngine::IsRunning() const {
  return running_.load();
}

bool SequenceEngine::RunOfflineCycle(StationId station) {
  if (IsRunning()) {
    EventBus::Instance().NotifyLog(
        QStringLiteral("offline cycle blocked: stop production engine first"));
    return false;
  }
  const auto& settings = AppContext::Instance().Settings();
  StationConfig cfg = (station == StationId::kR09) ? settings.station_r09 : settings.station_r05;
  CycleOptions options;
  options.capture_live = true;
  options.write_plc = true;
  return RunCycle(station, cfg, options);
}

bool SequenceEngine::RunReplayCycle(StationId station, const std::string& session_dir) {
  if (IsRunning()) {
    EventBus::Instance().NotifyLog(
        QStringLiteral("replay cycle blocked: stop production engine first"));
    return false;
  }
  if (session_dir.empty() || !SessionDirHasCaptureData(session_dir)) {
    EventBus::Instance().NotifyLog(
        QString("replay failed: invalid session dir %1").arg(QString::fromStdString(session_dir)));
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

void SequenceEngine::WorkerLoop() {
  bool last_r05 = false;
  bool last_r07 = false;
  bool last_r09 = false;

  while (running_.load()) {
    if (!plc_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    bool r05 = false;
    bool r07 = false;
    bool r09 = false;
    plc_->PollTrigger(StationId::kR05, &r05);
    plc_->PollTrigger(StationId::kR07, &r07);
    plc_->PollTrigger(StationId::kR09, &r09);

    // 上升沿触发，避免 PLC 信号保持期间重复跑周期
    const bool edge_r05 = r05 && !last_r05;
    const bool edge_r07 = r07 && !last_r07;
    const bool edge_r09 = r09 && !last_r09;
    last_r05 = r05;
    last_r07 = r07;
    last_r09 = r09;

    if (edge_r09) {
      EventBus::Instance().NotifyTrigger(StationId::kR09);
      CycleOptions options;
      RunCycle(StationId::kR09, AppContext::Instance().Settings().station_r09, options);
    } else if (edge_r05 || edge_r07) {
      EventBus::Instance().NotifyTrigger(StationId::kR05);
      CycleOptions options;
      RunCycle(StationId::kR05, AppContext::Instance().Settings().station_r05, options);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

bool SequenceEngine::RunCycle(StationId station, StationConfig station_cfg, const CycleOptions& options) {
  const auto& settings = AppContext::Instance().Settings();

  // --- 6.1 建立落盘上下文 ---
  // 在线：新建 {data}/{yyyyMMdd}/ + 时间戳前缀
  // 回放：复用已有 session_dir，解析文件名中的前缀
  CaptureRecordContext record_ctx =
      options.capture_live ? BuildCaptureRecordContext(station, settings.data_path)
                           : BuildReplayRecordContext(options.replay_session_dir, station);

  AlgoRequest req;
  req.station = station;
  req.session_dir = record_ctx.output_dir;
  req.transfer_flags = BuildAlgoTransferFlags(settings);

  // --- 6.2 采集或准备算法输入 ---
  if (options.capture_live) {
    // 在线 / 离线测试：内存采集 → SHM 直传深度/点云（受 transferDepth/transferPointcloud 控制）
    req.input_mode = AlgoInputMode::kOnlineShm;
    for (const auto& cam_id : station_cfg.camera_ids) {
      auto it = cameras_.find(cam_id);
      if (it == cameras_.end() || !it->second) {
        EventBus::Instance().NotifyLog(QString("camera missing: %1").arg(QString::fromStdString(cam_id)));
        continue;
      }
      CaptureBundle bundle = it->second->Capture();
      const std::string file_prefix = MakeCaptureFilePrefix(record_ctx, cam_id);
      if (!bundle.ok) {
        EventBus::Instance().NotifyLog(
            QString("capture failed cam=%1").arg(QString::fromStdString(cam_id)));
      } else {
        AssignCapturePaths(&bundle, record_ctx.output_dir, file_prefix);

        // 深度：实机走 RVC SaveDepthMap→tiff（须同步，SDK 帧仍有效）；仿真走 SaveLastCaptureToDir→pgm
        bool depth_saved = true;
        if (!bundle.depth_path.empty()) {
          depth_saved =
              it->second->SaveLastCaptureToDir(record_ctx.output_dir, file_prefix, &bundle);
          if (!depth_saved) {
            EventBus::Instance().NotifyLog(
                QString("save depth FAILED cam=%1 path=%2")
                    .arg(QString::fromStdString(cam_id),
                         QString::fromStdString(bundle.depth_path)));
          }
        }

        // 点云：仍异步写 ply（不占用算法等待）
        if (!bundle.pointcloud_path.empty()) {
          CaptureBundle ply_job;
          ply_job.ok = true;
          ply_job.camera_serial = bundle.camera_serial;
          ply_job.pointcloud = bundle.pointcloud;
          ply_job.pointcloud_path = bundle.pointcloud_path;
          CaptureSaveWorker::Instance().Enqueue(std::move(ply_job));
        }

        QStringList parts;
        parts << QString("capture stub cam=%1").arg(QString::fromStdString(cam_id));
        if (!bundle.depth_path.empty()) {
          parts << QString("depth_ok=%1 depth=%2")
                       .arg(depth_saved)
                       .arg(QString::fromStdString(bundle.depth_path));
        }
        if (!bundle.pointcloud_path.empty()) {
          parts << QString("ply=%1").arg(QString::fromStdString(bundle.pointcloud_path));
        }
        if (bundle.depth_path.empty() && bundle.pointcloud_path.empty()) {
          parts << QStringLiteral("(dataStub saveDepth/savePointcloud both off)");
        }
        EventBus::Instance().NotifyLog(parts.join(QLatin1Char(' ')));
      }
      req.captures.push_back(std::move(bundle));
    }
  } else {
    // 历史回放：仅填充磁盘路径，算法侧 PrepareAlgoInputFromPaths 读本地文件
    req.input_mode = AlgoInputMode::kOfflinePath;
    req.transfer_flags = 0;
    for (const auto& cam_id : station_cfg.camera_ids) {
      CaptureBundle bundle;
      bundle.camera_serial = cam_id;
      bundle.depth_path =
          FindDepthImageInSession(record_ctx.output_dir, record_ctx.station_tag, cam_id);
      bundle.ok = !bundle.depth_path.empty();
      req.captures.push_back(bundle);
    }
  }

  // --- 6.3 与算法交互（SHM 或进程内 Mock）---
  AlgoResponse algo_resp;
  bool algo_ok = false;
  if (algo_) {
    algo_ok = algo_->Run(req, &algo_resp, settings.algo_timeout_ms);
  }

  // --- 6.4 算法失败兜底：5 条 Log 置 NG ---
  if (!algo_ok) {
    algo_resp.logs.fill(LogResult{});
    for (auto& log : algo_resp.logs) {
      log.status = InspectStatus::kNg;
    }
  }

  // --- 6.5 同步保存算法结果 CSV（与采集异步落盘不同）---
  if (!SaveAlgoResultCsv(record_ctx, algo_resp.logs, options.algo_result_suffix)) {
    EventBus::Instance().NotifyLog(QString("algo result save failed"));
  }

  // --- 6.6 写入 PLC（回放模式跳过）---
  bool plc_ok = true;
  if (options.write_plc && plc_) {
    for (StationId target : CompletedStations(station)) {
      if (!plc_->WriteLogResults(target, algo_resp.logs)) {
        plc_ok = false;
      }
      if (!plc_->WriteSequenceCompleted(target, true)) {
        plc_ok = false;
      }
    }
  }

  // --- 6.7 MES 上报（当前 NullMesReporter 空实现）---
  if (mes_) {
    mes_->ReportCycle("", station, algo_resp.logs);
  }

  // --- 6.8 周期结束：通知 UI 刷新工位表与灰度图预览 ---
  CycleResultEvent ev;
  ev.station = station;
  ev.logs = algo_resp.logs;
  ev.session_dir = QString::fromStdString(record_ctx.output_dir);
  for (const auto& capture : req.captures) {
    if (capture.ok && capture.gray) {
      FillGrayPreview(&ev, *capture.gray);
      if (!ev.gray_bytes.isEmpty()) {
        break;
      }
    }
  }
  ev.plc_ok = plc_ok;
  ev.algo_ok = algo_ok;
  EventBus::Instance().NotifyCycleCompleted(ev);

  const QString mode = options.capture_live
                         ? (IsSimulationMode(AppContext::Instance().Settings()) ? QStringLiteral("simulation")
                                                                               : QStringLiteral("live"))
                         : QStringLiteral("replay");
  EventBus::Instance().NotifyLog(
      QString("cycle done mode=%1 station=%2 algo=%3 plc=%4 saved=%5")
          .arg(mode)
          .arg(static_cast<int>(station))
          .arg(algo_ok)
          .arg(options.write_plc ? plc_ok : true)
          .arg(QString::fromStdString(record_ctx.timestamp_prefix)));
  return algo_ok && (options.write_plc ? plc_ok : true);
}

}  // namespace visual

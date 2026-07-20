/**
 * @file sequence_engine.h
 * @brief 产线编排核心：触发 → 采集 → 算法 → 写 PLC → 通知 UI。
 *
 * 单次周期统一入口：RunCycle()（详见 docs/框架流程通路.md §六）
 *
 * 三种触发方式：
 *   在线产线   WorkerLoop() PLC 20ms 边沿 → RunCycle(capture_live=true,  write_plc=true)
 *   离线测试   RunOfflineCycle()          → RunCycle(capture_live=true,  write_plc=true)
 *   历史回放   RunReplayCycle()           → RunCycle(capture_live=false, write_plc=false)
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <thread>

#include "visual/app_context.h"
#include "visual/i_algo_service.h"
#include "visual/i_camera_3d.h"
#include "visual/i_mes_reporter.h"
#include "visual/i_plc_client.h"

namespace visual {

class SequenceEngine {
 public:
  SequenceEngine();
  ~SequenceEngine();

  void SetPlcClient(std::shared_ptr<IPlcClient> plc);
  void SetAlgoService(std::shared_ptr<IAlgoService> algo);
  void SetMesReporter(std::shared_ptr<IMesReporter> mes);
  void RegisterCamera(const std::string& id, CameraPtr camera);
  CameraPtr GetCamera(const std::string& id) const;

  /** 断开并释放全部已注册相机（退出进程前必须调用）。 */
  void DisconnectAllCameras();

  /** 阶段 1 续：连接 PLC、映射 SHM、启动 WorkerLoop 与 CaptureSaveWorker。 */
  bool Start();
  void Stop();
  bool IsRunning() const;

  /** 尝试连接 PLC（已连接则直接返回 true），并通知 EventBus。 */
  bool TryConnectPlc();
  bool IsPlcConnected() const;

  /**
   * 离线测试（实时采图）：跳过 PLC 触发，仍走在线 SHM + 写 PLC。
   * 入口：MainWindow 状态栏 OfflineTestWidget。
   */
  bool RunOfflineCycle(StationId station);

  /**
   * 历史数据回放：不采图，算法走 kOfflinePath（session_dir），不写 PLC。
   * 入口：MainWindow 菜单「离线测试 → 历史数据回放」。
   */
  bool RunReplayCycle(StationId station, const std::string& session_dir);

 private:
  /** 控制 RunCycle 分支：在线采图 / 回放路径 / 是否写 PLC / 结果文件名后缀。 */
  struct CycleOptions {
    bool capture_live = true;
    bool write_plc = true;
    std::string replay_session_dir;
    std::string algo_result_suffix = "algo_result";
  };

  /** 阶段 2 在线：20ms 轮询 PLC 触发位，上升沿调用 RunCycle。 */
  void WorkerLoop();

  /**
   * 单次视觉周期主流程（§六）：
   *   6.1 落盘上下文 → 6.2 采集/路径 → 6.3 算法 → 6.4 失败兜底
   *   → 6.5 结果 CSV → 6.6 写 PLC → 6.7 MES → 6.8 EventBus 通知 UI
   */
  bool RunCycle(StationId station, StationConfig station_cfg, const CycleOptions& options);

  std::shared_ptr<IPlcClient> plc_;
  std::shared_ptr<IAlgoService> algo_;
  std::shared_ptr<IMesReporter> mes_;
  std::map<std::string, CameraPtr> cameras_;

  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace visual

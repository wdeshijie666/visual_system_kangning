/**
 * @file sequence_engine.h
 * @brief 产线编排核心：触发 → 采集 → 算法 → 写 PLC → 通知 UI。
 *
 * 双工位并行：单 Poll 线程 + R05/R09 双任务队列 + 双执行线程；
 * 同工位互斥，异工位可同时 RunCycle。
 */
#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "visual/app_context.h"
#include "visual/cycle_job_queue.h"
#include "visual/fault_breaker.h"
#include "visual/i_algo_service.h"
#include "visual/i_camera_3d.h"
#include "visual/i_mes_reporter.h"
#include "visual/i_plc_client.h"
#include "visual/shm_algo_service.h"

namespace visual {

class SequenceEngine {
 public:
  SequenceEngine();
  ~SequenceEngine();

  void SetPlcClient(std::shared_ptr<IPlcClient> plc);
  /** 单实例算法（Mock）；同时作为 R05/R09 回退。 */
  void SetAlgoService(std::shared_ptr<IAlgoService> algo);
  /** 双通道 SHM 算法池（生产路径）。 */
  void SetAlgoPool(std::shared_ptr<ShmAlgoServicePool> pool);
  void SetMesReporter(std::shared_ptr<IMesReporter> mes);
  void RegisterCamera(const std::string& id, CameraPtr camera);
  CameraPtr GetCamera(const std::string& id) const;

  /** 断开并释放全部已注册相机（退出进程前必须调用）。 */
  void DisconnectAllCameras();

  /** 连接 PLC、映射 SHM、启动轮询线程与双周期工作线程。 */
  bool Start();
  /**
   * 停止产线：置 running_=false、唤醒队列、join 全部 worker。
   * 即使熔断已将 running_ 置 false，仍会 join，避免再次 Start 时 std::terminate。
   */
  void Stop();
  bool IsRunning() const;

  /** 尝试连接 PLC（已连接则直接返回 true），并通知 EventBus。 */
  bool TryConnectPlc();
  bool IsPlcConnected() const;

  /** 熔断后手动复位（例如操作员确认故障已排除）。 */
  void ResetFaultBreakers();

  /**
   * 离线测试（实时采图）：跳过 PLC 触发，仍走在线 SHM + 写 PLC。
   * 引擎运行中禁止调用。
   * 可由 UI 后台线程调用；同工位与产线 Worker 通过 CycleMutex 互斥。
   */
  bool RunOfflineCycle(StationId station);

  /**
   * 历史数据回放：不采图，算法走 kOfflinePath，不写 PLC。
   * 可由 UI 后台线程调用；同工位互斥同上。
   */
  bool RunReplayCycle(StationId station, const std::string& session_dir);

 private:
  struct CycleOptions {
    bool capture_live = true;
    bool write_plc = true;
    std::string replay_session_dir;
    std::string algo_result_suffix = "algo_result";
    /**
     * 手动/离线单通路验证：
     * - 只写回本工位 PLC（R05 不附带 R07）
     * - 不更新全局熔断计数
     * - 采图前对本工位相机尝试一次重连
     */
    bool single_station_only = false;
    bool update_fault_breaker = true;
  };

  struct PendingCycle {
    StationId station = StationId::kR05;
    StationConfig cfg;
    CycleOptions options;
  };

  /** 仅做 PLC 边沿检测；设备探活由独立 health 线程负责。 */
  void PollLoop();
  /** R05 通道周期消费者。 */
  void CycleWorkerLoopR05();
  /** R09 通道周期消费者。 */
  void CycleWorkerLoopR09();

  bool RunCycle(StationId station, StationConfig station_cfg, const CycleOptions& options);

  IAlgoService* ResolveAlgo(StationId station);
  std::mutex& CycleMutexFor(StationId station);
  CycleJobQueue<PendingCycle>& CycleQueueFor(StationId station);

  /** 相机/PLC 巡检：探活确认掉线后再重连；工位周期进行中则跳过该工位。 */
  void CheckDeviceHealth();
  /** 注册相机后启动；独立于产线 Start/Stop，未开产线也能空闲恢复。 */
  void StartDeviceHealthMonitor();
  void StopDeviceHealthMonitor();
  void DeviceHealthLoop();

  void OnCycleOutcome(bool capture_ok, bool algo_ok, bool plc_ok, const std::string& cycle_id,
                      bool update_fault_breaker = true);
  void NotifyQueuesStop();
  /** join poll/cycle 线程（可在 UI 线程调用；禁止在 cycle worker 内调用）。 */
  void JoinWorkerThreads();

  std::shared_ptr<IPlcClient> plc_;
  std::shared_ptr<IAlgoService> algo_;
  std::shared_ptr<ShmAlgoServicePool> algo_pool_;
  std::shared_ptr<IMesReporter> mes_;
  std::map<std::string, CameraPtr> cameras_;

  std::atomic<bool> running_{false};
  std::thread poll_thread_;
  std::thread cycle_thread_r05_;
  std::thread cycle_thread_r09_;
  CycleJobQueue<PendingCycle> cycle_queue_r05_{2};
  CycleJobQueue<PendingCycle> cycle_queue_r09_{2};

  /** 同工位：Worker 与 UI 离线/回放互斥；异工位可并行。 */
  std::mutex cycle_mutex_r05_;
  std::mutex cycle_mutex_r09_;

  FaultBreaker capture_breaker_{3};
  FaultBreaker algo_breaker_{3};
  FaultBreaker plc_breaker_{3};

  std::atomic<bool> health_monitor_enabled_{false};
  std::thread health_thread_;
  /** 串行化巡检，避免与退出 Disconnect 交错。 */
  std::mutex health_mutex_;
};

}  // namespace visual

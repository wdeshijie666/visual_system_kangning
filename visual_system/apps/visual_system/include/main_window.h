#pragma once

#include <memory>

#include <QList>
#include <QMainWindow>
#include <QSize>

#include "visual/event_bus.h"
#include "visual/sequence_engine.h"

class QSplitter;
class QAction;
class QMenu;
class QPushButton;

/** 启动完成后记录的窗口与各 Splitter 布局快照。 */
struct WindowLayoutSnapshot {
  bool valid = false;
  QSize window_size;
  Qt::WindowStates window_state = Qt::WindowNoState;
  QSize central_size;
  QList<int> main_sizes;
  QList<int> center_sizes;
  QList<int> viewport_sizes;
  QList<int> result_sizes;
  QList<int> right_sizes;
};

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(std::shared_ptr<visual::SequenceEngine> engine, bool simulation_mode = false,
                      QWidget* parent = nullptr);
  void InitUi();

 private slots:
  void OnCycleCompleted(const visual::CycleResultEvent& event);
  void OnLogLine(const QString& line);
  void OnStartEngine();
  void OnStopEngine();
  void OnResetWindowLayout();
  void OnReplayR05();
  void OnReplayR09();

 private:
  void LoadTheme();
  void CaptureWindowLayoutSnapshot();
  void ResetAllSplitters();
  void RunHistoricalReplay(visual::StationId station);
  /** 后台线程执行手动周期（采图+算法），结束后回 UI 线程清 Busy。 */
  void StartAsyncOfflineCycle(visual::StationId station);
  /**
   * 占用/释放离线操作门禁：Busy 期间禁用手动、回放、启动产线，防止连点与并发。
   * 产线 Worker 路径不受影响（启动后本就会关掉离线入口）。
   */
  void SetOfflineOpBusy(bool busy);
  void FinishOfflineOp(const QString& status_message);
  void UpdateOfflineTestEnabled(bool enabled);
  /** 更新产线启动/停止按钮的可用性与样式（二者互斥）。 */
  void UpdateEngineControlState(bool running);
  /** 生产模式：校验 PLC/相机/算法，通过返回 true。 */
  bool EnsureProductionDevicesReady(QString* reason);
  /** 生产模式：按当前设备状态禁用/启用「启动」按钮（不探测 PLC）。 */
  void ApplyProductionStartInterlock();

 protected:
  void showEvent(QShowEvent* event) override;

  std::shared_ptr<visual::SequenceEngine> engine_;
  bool simulation_mode_ = false;
  /** UI 线程标志：手动触发或历史回放异步任务进行中。 */
  bool offline_op_busy_ = false;
  QSplitter* main_split_ = nullptr;
  QSplitter* center_split_ = nullptr;
  QSplitter* viewport_split_ = nullptr;
  QSplitter* result_split_ = nullptr;
  QSplitter* right_split_ = nullptr;
  WindowLayoutSnapshot layout_snapshot_;
  class StationResultWidget* r05_table_ = nullptr;
  class StationResultWidget* r09_table_ = nullptr;
  class ViewportWidget* r05_viewport_ = nullptr;
  class ViewportWidget* r09_viewport_ = nullptr;
  class DeviceStatusWidget* device_status_ = nullptr;
  class CameraManagerWidget* camera_manager_ = nullptr;
  class QTextEdit* log_view_ = nullptr;
  class OfflineTestWidget* offline_test_ = nullptr;
  QMenu* offline_menu_ = nullptr;
  QAction* replay_r05_action_ = nullptr;
  QAction* replay_r09_action_ = nullptr;
  QPushButton* start_engine_button_ = nullptr;
  QPushButton* stop_engine_button_ = nullptr;
};

#include "main_window.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>

#include <thread>

#include "camera_manager_widget.h"
#include "device_status_widget.h"
#include "offline_test_widget.h"
#include "station_result_widget.h"
#include "viewport_widget.h"
#include "visual/alarm_service.h"
#include "visual/app_context.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"
#include "visual/sequence_engine.h"

namespace {

QSplitter* MakeSplitter(Qt::Orientation orientation, QWidget* parent = nullptr) {
  auto* splitter = new QSplitter(orientation, parent);
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(6);
  splitter->setContentsMargins(0, 0, 0, 0);
  splitter->setFrameShape(QFrame::NoFrame);
  return splitter;
}

void RefreshLayout(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  widget->updateGeometry();
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

int SplitterHandleSpace(QSplitter* splitter) {
  if (splitter == nullptr) {
    return 0;
  }
  return splitter->handleWidth() * qMax(0, splitter->count() - 1);
}

int SplitterAvailableExtent(QSplitter* splitter) {
  if (splitter == nullptr) {
    return 0;
  }
  const int handle_space = SplitterHandleSpace(splitter);
  const int extent = (splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height()) -
                     handle_space;
  return qMax(0, extent);
}

QList<int> ScaleSavedSizesToExtent(const QList<int>& saved_sizes, int extent) {
  QList<int> sizes;
  if (extent <= 0 || saved_sizes.isEmpty()) {
    return sizes;
  }

  int saved_sum = 0;
  for (int value : saved_sizes) {
    saved_sum += value;
  }
  if (saved_sum <= 0) {
    return sizes;
  }

  int used = 0;
  for (int i = 0; i < saved_sizes.size(); ++i) {
    if (i + 1 == saved_sizes.size()) {
      sizes.append(qMax(1, extent - used));
    } else {
      const int size =
          qMax(1, static_cast<int>(qRound64(static_cast<double>(extent) * saved_sizes[i] / saved_sum)));
      sizes.append(size);
      used += size;
    }
  }
  return sizes;
}

void ApplySavedSizes(QSplitter* splitter, const QList<int>& saved_sizes) {
  if (splitter == nullptr || saved_sizes.isEmpty()) {
    return;
  }
  const int extent = SplitterAvailableExtent(splitter);
  if (extent <= 0) {
    return;
  }
  splitter->setSizes(ScaleSavedSizesToExtent(saved_sizes, extent));
}

/** 主布局 stretch(0,1,0)：左右保持快照宽度，中间占据剩余空间。 */
void ApplyMainSplitReset(QSplitter* main_split, const WindowLayoutSnapshot& snapshot) {
  if (main_split == nullptr || snapshot.main_sizes.size() < 3) {
    return;
  }
  const int extent = SplitterAvailableExtent(main_split);
  if (extent <= 0 || snapshot.central_size.width() <= 0) {
    return;
  }

  const int saved_left = snapshot.main_sizes[0];
  const int saved_right = snapshot.main_sizes[2];

  if (main_split->width() >= snapshot.central_size.width()) {
    int left = qMin(saved_left, extent - 1);
    int right = qMin(saved_right, extent - left - 1);
    int center = qMax(1, extent - left - right);
    main_split->setSizes({left, center, right});
    return;
  }

  main_split->setSizes(ScaleSavedSizesToExtent(snapshot.main_sizes, extent));
}

bool SizeCloseEnough(const QSize& lhs, const QSize& rhs, int tolerance = 2) {
  return qAbs(lhs.width() - rhs.width()) <= tolerance && qAbs(lhs.height() - rhs.height()) <= tolerance;
}

}  // namespace

MainWindow::MainWindow(std::shared_ptr<visual::SequenceEngine> engine, bool simulation_mode, QWidget* parent)
    : QMainWindow(parent), engine_(std::move(engine)), simulation_mode_(simulation_mode) {}

void MainWindow::LoadTheme() {
  QFile f(":/dark.qss");
  if (f.open(QIODevice::ReadOnly)) {
    qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
  }
}

void MainWindow::InitUi() {
  LoadTheme();
  const auto& settings = visual::AppContext::Instance().Settings();
  const QString mode_suffix = simulation_mode_ ? tr(" [仿真]") : tr(" [实机]");
  setWindowTitle(QString::fromStdString(settings.app_name) + mode_suffix);

  auto* file_menu = menuBar()->addMenu(tr("文件"));
  file_menu->addAction(tr("退出"), this, &QWidget::close);
  menuBar()->addMenu(tr("设备"));

  auto* offline_menu = menuBar()->addMenu(tr("离线测试"));
  offline_menu_ = offline_menu;
  replay_r05_action_ = offline_menu->addAction(tr("历史数据回放 R05"), this, &MainWindow::OnReplayR05);
  replay_r09_action_ = offline_menu->addAction(tr("历史数据回放 R09"), this, &MainWindow::OnReplayR09);

  auto* window_menu = menuBar()->addMenu(tr("视窗"));
  window_menu->addAction(tr("重置视窗"), this, &MainWindow::OnResetWindowLayout);
  menuBar()->addMenu(tr("帮助"));

  auto* toolbar = addToolBar(tr("工具"));
  toolbar->setMovable(false);
  start_engine_button_ = new QPushButton(tr("启动"), this);
  start_engine_button_->setObjectName(QStringLiteral("btnEngineStart"));
  stop_engine_button_ = new QPushButton(tr("停止"), this);
  stop_engine_button_->setObjectName(QStringLiteral("btnEngineStop"));
  toolbar->addWidget(start_engine_button_);
  toolbar->addWidget(stop_engine_button_);
  connect(start_engine_button_, &QPushButton::clicked, this, &MainWindow::OnStartEngine);
  connect(stop_engine_button_, &QPushButton::clicked, this, &MainWindow::OnStopEngine);

  main_split_ = MakeSplitter(Qt::Horizontal, this);

  auto* left_split = MakeSplitter(Qt::Vertical, main_split_);
  device_status_ = new DeviceStatusWidget(left_split);
  left_split->addWidget(device_status_);
  left_split->setMinimumWidth(180);

  center_split_ = MakeSplitter(Qt::Vertical, main_split_);

  viewport_split_ = MakeSplitter(Qt::Horizontal, center_split_);
  r05_viewport_ = new ViewportWidget(tr("R05 灰度图"), viewport_split_);
  r09_viewport_ = new ViewportWidget(tr("R09 灰度图"), viewport_split_);
  viewport_split_->addWidget(r05_viewport_);
  viewport_split_->addWidget(r09_viewport_);
  viewport_split_->setStretchFactor(0, 1);
  viewport_split_->setStretchFactor(1, 1);

  result_split_ = MakeSplitter(Qt::Horizontal, center_split_);
  r05_table_ = new StationResultWidget("R05", result_split_);
  r09_table_ = new StationResultWidget("R09", result_split_);
  result_split_->addWidget(r05_table_);
  result_split_->addWidget(r09_table_);
  result_split_->setStretchFactor(0, 1);
  result_split_->setStretchFactor(1, 1);

  center_split_->addWidget(viewport_split_);
  center_split_->addWidget(result_split_);
  center_split_->setStretchFactor(0, 3);
  center_split_->setStretchFactor(1, 2);

  right_split_ = MakeSplitter(Qt::Vertical, main_split_);
  camera_manager_ = new CameraManagerWidget(right_split_);
  log_view_ = new QTextEdit(right_split_);
  log_view_->setReadOnly(true);
  right_split_->addWidget(camera_manager_);
  right_split_->addWidget(log_view_);
  right_split_->setMinimumWidth(220);

  main_split_->addWidget(left_split);
  main_split_->addWidget(center_split_);
  main_split_->addWidget(right_split_);
  main_split_->setStretchFactor(0, 0);
  main_split_->setStretchFactor(1, 1);
  main_split_->setStretchFactor(2, 0);
  main_split_->setSizes({280, 900, 320});

  center_split_->setSizes({520, 280});
  viewport_split_->setSizes({500, 500});
  result_split_->setSizes({500, 500});
  right_split_->setSizes({420, 220});

  setCentralWidget(main_split_);

  // 手动触发：后台线程跑 RunOfflineCycle，UI 只做 Busy 门禁，避免采图/算法卡住界面
  offline_test_ = new OfflineTestWidget(this);
  offline_test_->SetRunHandler([this](int station) {
    StartAsyncOfflineCycle(static_cast<visual::StationId>(station));
  });
  statusBar()->addPermanentWidget(offline_test_);
  UpdateEngineControlState(false);

  connect(&visual::EventBus::Instance(), &visual::EventBus::CycleCompleted, this,
          &MainWindow::OnCycleCompleted);
  connect(&visual::EventBus::Instance(), &visual::EventBus::LogLine, this, &MainWindow::OnLogLine);
  connect(&visual::EventBus::Instance(), &visual::EventBus::PlcStatusChanged, this,
          [this](bool connected, bool heartbeat) {
            if (device_status_ != nullptr) {
              device_status_->SetPlcStatus(connected, heartbeat);
            }
            ApplyProductionStartInterlock();
          });
  connect(&visual::EventBus::Instance(), &visual::EventBus::CameraStatusChanged, this,
          [this](const QString& id, bool connected) {
            if (device_status_ != nullptr) {
              device_status_->SetCameraStatus(id, connected);
            }
            ApplyProductionStartInterlock();
          });
  connect(&visual::EventBus::Instance(), &visual::EventBus::AlgoProcessStatusChanged, this,
          [this](bool running, const QString& detail) {
            if (device_status_ != nullptr) {
              device_status_->SetAlgoStatus(running, detail);
            }
            ApplyProductionStartInterlock();
          });

  // main.cpp 在 InitUi 订阅前就 NotifyCameraStatus，事件会丢失；此处按引擎实际连接状态刷新
  if (engine_) {
    for (const auto& kv : visual::AppContext::Instance().Devices()) {
      auto cam = engine_->GetCamera(kv.second.id);
      const bool connected = cam && cam->IsConnected();
      device_status_->SetCameraStatus(QString::fromStdString(kv.second.id), connected);
    }
    device_status_->SetPlcStatus(engine_->IsPlcConnected(), engine_->IsPlcConnected());
  }

  // 勿强制写成“初始化/未运行”：main 里算法可能已启动，信号可能在订阅前发出
  if (visual::AppContext::Instance().Settings().use_shm_algo) {
    const bool ready = visual::EventBus::IsAlgoProcessReady();
    device_status_->SetAlgoStatus(ready, ready ? tr("运行中") : tr("未就绪"));
  } else {
    // 进程内 Mock 视为算法就绪（无独立进程）
    device_status_->SetAlgoStatus(true, simulation_mode_ ? tr("进程内 Mock 仿真") : tr("进程内 Mock"));
  }

  ApplyProductionStartInterlock();

  camera_manager_->SetImportRecipeHandler([this](const QString& path) {
    if (!engine_) {
      return;
    }
    const std::string utf8_path = path.toUtf8().constData();
    const auto& devices = visual::AppContext::Instance().Devices();
    int loaded = 0;
    visual::RecipeParamList params;
    for (const auto& kv : devices) {
      auto cam = engine_->GetCamera(kv.second.id);
      visual::RecipeParamList one;
      if (cam && cam->LoadRecipeFile(utf8_path, &one)) {
        ++loaded;
        if (params.empty() && !one.empty()) {
          params = std::move(one);
        }
      }
    }
    if (loaded > 0) {
      if (!params.empty()) {
        camera_manager_->SetRecipeParams(params);
      }
      visual::EventBus::Instance().NotifyLog(
          QStringLiteral("配方导入成功: %1 → %2 台相机").arg(path).arg(loaded));
      statusBar()->showMessage(tr("配方导入成功"), 3000);
    } else {
      visual::EventBus::Instance().NotifyLog(
          QStringLiteral("配方导入失败: %1（请确认相机已连接）").arg(path));
      statusBar()->showMessage(tr("配方导入失败"), 5000);
    }
  });

  qRegisterMetaType<visual::AlarmRecord>("visual::AlarmRecord");
  connect(&visual::AlarmService::Instance(), &visual::AlarmService::AlarmRaised, this,
          [this](const visual::AlarmRecord& rec) {
            const QString level = rec.level == visual::AlarmLevel::kCritical
                                      ? tr("严重")
                                      : (rec.level == visual::AlarmLevel::kWarning ? tr("警告")
                                                                                  : tr("信息"));
            const QString line =
                QStringLiteral("[%1][%2] %3").arg(level, rec.subsystem, rec.message);
            OnLogLine(line);
            statusBar()->showMessage(line, 8000);
            if (rec.level == visual::AlarmLevel::kCritical) {
              QApplication::beep();
              if (engine_) {
                UpdateEngineControlState(engine_->IsRunning());
              }
            }
          });

  statusBar()->showMessage(tr("就绪"));
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (!layout_snapshot_.valid) {
    QTimer::singleShot(0, this, [this]() {
      QTimer::singleShot(50, this, [this]() { CaptureWindowLayoutSnapshot(); });
    });
  }
}

void MainWindow::CaptureWindowLayoutSnapshot() {
  if (main_split_ == nullptr) {
    return;
  }

  RefreshLayout(centralWidget());

  layout_snapshot_.window_size = size();
  layout_snapshot_.window_state = windowState();
  layout_snapshot_.central_size = main_split_->size();
  layout_snapshot_.main_sizes = main_split_->sizes();
  layout_snapshot_.center_sizes = center_split_->sizes();
  layout_snapshot_.viewport_sizes = viewport_split_->sizes();
  layout_snapshot_.result_sizes = result_split_->sizes();
  layout_snapshot_.right_sizes = right_split_->sizes();
  layout_snapshot_.valid = layout_snapshot_.main_sizes.size() >= 3 &&
                           layout_snapshot_.center_sizes.size() >= 2 &&
                           layout_snapshot_.central_size.width() > 0 &&
                           layout_snapshot_.central_size.height() > 0;
}

void MainWindow::OnCycleCompleted(const visual::CycleResultEvent& event) {
  // 阶段 6.8：EventBus → 刷新工位结果表与深度图视口
  if (event.station == visual::StationId::kR09) {
    r09_table_->UpdateResults(event.logs);
    r09_viewport_->SetGrayImage(event.gray_bytes, event.gray_width, event.gray_height);
  } else {
    r05_table_->UpdateResults(event.logs);
    r05_viewport_->SetGrayImage(event.gray_bytes, event.gray_width, event.gray_height);
  }
  statusBar()->showMessage(tr("周期完成 station=%1 algo=%2 plc=%3")
                               .arg(static_cast<int>(event.station))
                               .arg(event.algo_ok)
                               .arg(event.plc_ok));
}

void MainWindow::OnLogLine(const QString& line) {
  log_view_->append(line);
}

void MainWindow::OnStartEngine() {
  if (!engine_ || engine_->IsRunning()) {
    return;
  }
  // 手动/回放占用中禁止开产线，避免与同工位 CycleMutex / 相机并发
  if (offline_op_busy_) {
    statusBar()->showMessage(tr("手动或回放进行中，请等待结束后再启动产线"), 5000);
    return;
  }

  // 生产模式防呆：PLC / 相机 / 算法任一异常则禁止启动在线运行（手动/离线不受影响）
  if (!simulation_mode_) {
    QString reason;
    if (!EnsureProductionDevicesReady(&reason)) {
      const QString msg = tr("设备状态异常，无法启动在线运行：%1").arg(reason);
      visual::EventBus::Instance().NotifyLog(msg);
      statusBar()->showMessage(msg, 8000);
      ApplyProductionStartInterlock();
      return;
    }
  }

  engine_->Start();
  engine_->ResetFaultBreakers();
  device_status_->SetPlcStatus(engine_->IsPlcConnected(), engine_->IsPlcConnected());
  UpdateEngineControlState(true);
  statusBar()->showMessage(tr("SequenceEngine 已启动（离线测试已禁用）"));
}

void MainWindow::OnStopEngine() {
  if (!engine_ || !engine_->IsRunning()) {
    return;
  }
  engine_->Stop();
  device_status_->SetPlcStatus(engine_->IsPlcConnected(), engine_->IsPlcConnected());
  UpdateEngineControlState(false);
  statusBar()->showMessage(tr("SequenceEngine 已停止"));
}

bool MainWindow::EnsureProductionDevicesReady(QString* reason) {
  auto set_reason = [&](const QString& text) {
    if (reason != nullptr) {
      *reason = text;
    }
  };

  const auto& settings = visual::AppContext::Instance().Settings();
  // 以引擎实时状态为准刷新相机（仅校验已启用工位）
  if (engine_ != nullptr) {
    for (const auto& kv : visual::AppContext::Instance().Devices()) {
      const bool station_enabled =
          (kv.second.station == "r09" || kv.second.station == "R09")
              ? settings.station_r09.enabled
              : settings.station_r05.enabled;
      if (!station_enabled) {
        continue;
      }
      auto cam = engine_->GetCamera(kv.second.id);
      const bool connected = cam && cam->IsConnected();
      if (device_status_ != nullptr) {
        device_status_->SetCameraStatus(QString::fromStdString(kv.second.id), connected);
      }
      if (!connected) {
        set_reason(tr("相机未连接(%1)").arg(QString::fromStdString(kv.second.id)));
        return false;
      }
    }
  } else {
    set_reason(tr("编排引擎未就绪"));
    return false;
  }

  const bool require_algo_process = visual::AppContext::Instance().Settings().use_shm_algo;
  if (require_algo_process) {
    if (device_status_ == nullptr || !device_status_->IsAlgoOk()) {
      set_reason(tr("算法服务未运行"));
      return false;
    }
  }

  if (!engine_->TryConnectPlc()) {
    if (device_status_ != nullptr) {
      device_status_->SetPlcStatus(false, false);
    }
    set_reason(tr("PLC未连接"));
    return false;
  }
  if (device_status_ != nullptr) {
    device_status_->SetPlcStatus(true, true);
  }
  return true;
}

void MainWindow::ApplyProductionStartInterlock() {
  if (start_engine_button_ == nullptr || engine_ == nullptr || engine_->IsRunning()) {
    return;
  }
  // Busy 时强制禁启动，避免与手动周期抢资源
  if (offline_op_busy_) {
    start_engine_button_->setEnabled(false);
    start_engine_button_->setToolTip(tr("手动或回放进行中，无法启动产线"));
    return;
  }
  if (simulation_mode_) {
    start_engine_button_->setEnabled(true);
    start_engine_button_->setToolTip(QString());
    return;
  }

  // 按钮侧预检：相机 + 算法（PLC 在点击启动时再探测连接）
  QStringList faults;
  if (device_status_ != nullptr) {
    if (!device_status_->AreCamerasOk()) {
      faults << tr("相机未连接");
    }
    if (visual::AppContext::Instance().Settings().use_shm_algo && !device_status_->IsAlgoOk()) {
      faults << tr("算法服务异常");
    }
  }
  const bool ready = faults.isEmpty();
  start_engine_button_->setEnabled(ready);
  start_engine_button_->setToolTip(
      ready ? QString() : tr("设备状态异常，无法启动：%1").arg(faults.join(QStringLiteral("；"))));
}

void MainWindow::UpdateEngineControlState(bool running) {
  if (start_engine_button_ != nullptr) {
    // 产线已跑 或 离线 Busy：都不能点启动
    start_engine_button_->setEnabled(!running && !offline_op_busy_);
    start_engine_button_->setProperty("running", running);
    start_engine_button_->style()->unpolish(start_engine_button_);
    start_engine_button_->style()->polish(start_engine_button_);
    start_engine_button_->update();
  }
  if (stop_engine_button_ != nullptr) {
    stop_engine_button_->setEnabled(running);
    stop_engine_button_->setProperty("running", running);
    stop_engine_button_->style()->unpolish(stop_engine_button_);
    stop_engine_button_->style()->polish(stop_engine_button_);
    stop_engine_button_->update();
  }
  if (!running) {
    ApplyProductionStartInterlock();
  }
  UpdateOfflineTestEnabled(!running);
}

void MainWindow::UpdateOfflineTestEnabled(bool enabled) {
  // 产线运行 或 Busy：离线入口一律关闭
  const bool allow = enabled && !offline_op_busy_;
  if (offline_test_ != nullptr) {
    offline_test_->SetOfflineTestEnabled(allow);
  }
  if (replay_r05_action_ != nullptr) {
    replay_r05_action_->setEnabled(allow);
  }
  if (replay_r09_action_ != nullptr) {
    replay_r09_action_->setEnabled(allow);
  }
  if (offline_menu_ != nullptr) {
    offline_menu_->setToolTipsVisible(true);
    if (offline_op_busy_) {
      offline_menu_->setToolTip(tr("手动或回放进行中，请等待结束"));
    } else {
      offline_menu_->setToolTip(enabled ? QString() : tr("产线运行中，请先停止后再进行离线测试"));
    }
  }
}

void MainWindow::SetOfflineOpBusy(bool busy) {
  if (offline_op_busy_ == busy) {
    return;
  }
  offline_op_busy_ = busy;
  if (offline_test_ != nullptr) {
    offline_test_->SetBusy(busy);
  }
  // 按当前产线状态重算启动/离线入口（内部会读 offline_op_busy_）
  const bool running = engine_ && engine_->IsRunning();
  UpdateEngineControlState(running);
}

void MainWindow::FinishOfflineOp(const QString& status_message) {
  SetOfflineOpBusy(false);
  statusBar()->showMessage(status_message, 5000);
}

void MainWindow::StartAsyncOfflineCycle(visual::StationId station) {
  if (!engine_ || offline_op_busy_) {
    return;
  }
  if (engine_->IsRunning()) {
    // 与 OfflineTestWidget 产线禁用双保险
    statusBar()->showMessage(tr("产线运行中，请先停止后再手动触发"), 5000);
    return;
  }

  SetOfflineOpBusy(true);
  statusBar()->showMessage(tr("手动周期进行中…"));

  // 持有 engine 的 shared_ptr：窗口关闭后周期仍可安全跑完；回 UI 用 invokeMethod，勿在裸线程里 QTimer
  const auto engine = engine_;
  QPointer<MainWindow> self(this);
  std::thread([self, engine, station]() {
    const bool ok = engine->RunOfflineCycle(station);
    if (!self) {
      return;
    }
    MainWindow* raw = self.data();
    QMetaObject::invokeMethod(
        raw,
        [self, ok]() {
          if (!self) {
            return;
          }
          self->FinishOfflineOp(ok ? QObject::tr("手动周期完成") : QObject::tr("手动周期失败"));
        },
        Qt::QueuedConnection);
  }).detach();
}

void MainWindow::ResetAllSplitters() {
  if (!layout_snapshot_.valid || main_split_ == nullptr) {
    return;
  }

  RefreshLayout(centralWidget());
  const QSize current_central = main_split_->size();
  if (current_central.width() <= 0 || current_central.height() <= 0) {
    return;
  }

  if (SizeCloseEnough(current_central, layout_snapshot_.central_size)) {
    main_split_->setSizes(layout_snapshot_.main_sizes);
    center_split_->setSizes(layout_snapshot_.center_sizes);
    viewport_split_->setSizes(layout_snapshot_.viewport_sizes);
    result_split_->setSizes(layout_snapshot_.result_sizes);
    right_split_->setSizes(layout_snapshot_.right_sizes);
    return;
  }

  ApplyMainSplitReset(main_split_, layout_snapshot_);
  RefreshLayout(main_split_);

  ApplySavedSizes(center_split_, layout_snapshot_.center_sizes);
  RefreshLayout(center_split_);

  ApplySavedSizes(viewport_split_, layout_snapshot_.viewport_sizes);
  ApplySavedSizes(result_split_, layout_snapshot_.result_sizes);
  ApplySavedSizes(right_split_, layout_snapshot_.right_sizes);
}

void MainWindow::OnResetWindowLayout() {
  ResetAllSplitters();
  statusBar()->showMessage(tr("视窗布局已重置 (%1x%2, 基准 %3x%4)")
                               .arg(size().width())
                               .arg(size().height())
                               .arg(layout_snapshot_.window_size.width())
                               .arg(layout_snapshot_.window_size.height()));
}

void MainWindow::RunHistoricalReplay(visual::StationId station) {
  if (!engine_) {
    return;
  }
  if (offline_op_busy_) {
    statusBar()->showMessage(tr("手动或回放进行中，请等待结束"), 5000);
    return;
  }
  if (engine_->IsRunning()) {
    statusBar()->showMessage(tr("产线运行中，无法进行历史回放"));
    visual::EventBus::Instance().NotifyLog(QStringLiteral("产线运行中，历史回放已拒绝"));
    return;
  }
  // 目录选择必须在 UI 线程；真正回放放到后台，避免算法阻塞界面
  const QString default_dir =
      QString::fromStdString(visual::ResolveDataRoot(visual::AppContext::Instance().Settings().data_path));
  const QString session_dir =
      QFileDialog::getExistingDirectory(this, tr("选择历史数据目录"), default_dir);
  if (session_dir.isEmpty()) {
    return;
  }

  SetOfflineOpBusy(true);
  statusBar()->showMessage(tr("历史回放进行中…"));

  const auto engine = engine_;
  const std::string session = session_dir.toStdString();
  QPointer<MainWindow> self(this);
  std::thread([self, engine, station, session]() {
    const bool ok = engine->RunReplayCycle(station, session);
    if (!self) {
      return;
    }
    MainWindow* raw = self.data();
    QMetaObject::invokeMethod(
        raw,
        [self, ok]() {
          if (!self) {
            return;
          }
          self->FinishOfflineOp(ok ? QObject::tr("历史回放完成") : QObject::tr("历史回放失败"));
        },
        Qt::QueuedConnection);
  }).detach();
}

void MainWindow::OnReplayR05() {
  RunHistoricalReplay(visual::StationId::kR05);
}

void MainWindow::OnReplayR09() {
  RunHistoricalReplay(visual::StationId::kR09);
}

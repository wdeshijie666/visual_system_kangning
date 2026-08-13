#include "main_window.h"

#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QIcon>
#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
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
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <thread>
#include <vector>

#include "camera_manager_widget.h"
#include "device_status_widget.h"
#include "offline_test_widget.h"
#include "repeatability_test_dialog.h"
#include "station_result_widget.h"
#include "viewport_widget.h"
#include "visual/alarm_service.h"
#include "visual/app_context.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"
#include "visual/log_format.h"
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

/** 主布局 stretch：左侧保持快照宽度，中间占剩余空间。 */
void ApplyMainSplitReset(QSplitter* main_split, const WindowLayoutSnapshot& snapshot) {
  if (main_split == nullptr || snapshot.main_sizes.size() < 2) {
    return;
  }
  const int extent = SplitterAvailableExtent(main_split);
  if (extent <= 0 || snapshot.central_size.width() <= 0) {
    return;
  }

  const int saved_left = snapshot.main_sizes[0];
  if (main_split->width() >= snapshot.central_size.width()) {
    int left = qMin(saved_left, extent - 1);
    int center = qMax(1, extent - left);
    main_split->setSizes({left, center});
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

MainWindow::~MainWindow() {
  PrepareForAppExit();
}

void MainWindow::PrepareForAppExit() {
  JoinOfflineWorker();
  offline_op_busy_ = false;
}

void MainWindow::JoinOfflineWorker() {
  if (offline_worker_.joinable()) {
    offline_worker_.join();
  }
}

void MainWindow::LoadTheme() {
  QFile f(":/dark.qss");
  if (f.open(QIODevice::ReadOnly)) {
    qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
  }
}

void MainWindow::InitUi() {
  LoadTheme();
  // 标题：左产品名，右公司名（标准标题栏用 "—" 分隔）
  setWindowTitle(QStringLiteral("SmartGuide — TanlyMind"));
  setWindowIcon(QIcon(QStringLiteral(":/icons/标题栏/应用Logo.svg")));

  auto* file_menu = menuBar()->addMenu(tr("文件"));
  file_menu->addAction(tr("退出"), this, &QWidget::close);

  auto* device_menu = menuBar()->addMenu(tr("设备"));
  device_menu->addAction(tr("2D相机参数导入"), this, &MainWindow::OnOpen2DCameraImport);

  auto* offline_menu = menuBar()->addMenu(tr("离线测试"));
  offline_menu_ = offline_menu;
  replay_r05_action_ = offline_menu->addAction(tr("历史数据回放 R05"), this, &MainWindow::OnReplayR05);
  replay_r09_action_ = offline_menu->addAction(tr("历史数据回放 R09"), this, &MainWindow::OnReplayR09);
  offline_menu->addSeparator();
  repeatability_action_ =
      offline_menu->addAction(tr("重复精度测试…"), this, &MainWindow::OnRepeatabilityTest);

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
  toolbar->addSeparator();
  offline_test_ = new OfflineTestWidget(this);
  offline_test_->SetRunHandler([this](int station) {
    StartAsyncOfflineCycle(static_cast<visual::StationId>(station));
  });
  toolbar->addWidget(offline_test_);
  connect(start_engine_button_, &QPushButton::clicked, this, &MainWindow::OnStartEngine);
  connect(stop_engine_button_, &QPushButton::clicked, this, &MainWindow::OnStopEngine);

  // 左 20%：设备状态 + 日志；中 80%：灰度图(7) + 结果表(3)
  main_split_ = MakeSplitter(Qt::Horizontal, this);

  left_split_ = MakeSplitter(Qt::Vertical, main_split_);
  device_status_ = new DeviceStatusWidget(left_split_);
  log_view_ = new QTextEdit(left_split_);
  log_view_->setReadOnly(true);
  left_split_->addWidget(device_status_);
  left_split_->addWidget(log_view_);
  left_split_->setStretchFactor(0, 3);
  left_split_->setStretchFactor(1, 7);
  left_split_->setMinimumWidth(180);

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
  center_split_->setStretchFactor(0, 7);
  center_split_->setStretchFactor(1, 3);

  main_split_->addWidget(left_split_);
  main_split_->addWidget(center_split_);
  main_split_->setStretchFactor(0, 0);
  main_split_->setStretchFactor(1, 1);
  // 左:中 = 20:80；图:表 = 7:3
  main_split_->setSizes({240, 960});
  left_split_->setSizes({210, 490});
  center_split_->setSizes({560, 240});
  viewport_split_->setSizes({480, 480});
  result_split_->setSizes({480, 480});

  setCentralWidget(main_split_);

  station_r05_status_ = new QLabel(tr("R05: 空闲"), this);
  station_r09_status_ = new QLabel(tr("R09: 空闲"), this);
  station_r05_idle_timer_ = new QTimer(this);
  station_r09_idle_timer_ = new QTimer(this);
  station_r05_idle_timer_->setSingleShot(true);
  station_r09_idle_timer_->setSingleShot(true);
  connect(station_r05_idle_timer_, &QTimer::timeout, this, [this]() {
    SetStationUiStatus(visual::StationId::kR05, StationCycleUiStatus::kIdle);
  });
  connect(station_r09_idle_timer_, &QTimer::timeout, this, [this]() {
    SetStationUiStatus(visual::StationId::kR09, StationCycleUiStatus::kIdle);
  });
  statusBar()->addPermanentWidget(station_r05_status_);
  statusBar()->addPermanentWidget(station_r09_status_);
  UpdateEngineControlState(false);

  connect(&visual::EventBus::Instance(), &visual::EventBus::CycleStarted, this,
          &MainWindow::OnCycleStarted);
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

  if (visual::AppContext::Instance().Settings().use_shm_algo) {
    const bool ready = visual::EventBus::IsAlgoProcessReady();
    device_status_->SetAlgoStatus(ready, ready ? tr("运行中") : tr("未就绪"));
  } else {
    device_status_->SetAlgoStatus(true, simulation_mode_ ? tr("进程内 Mock 仿真") : tr("进程内 Mock"));
  }

  ApplyProductionStartInterlock();

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
  // 初始化完成后自动启动；会等到算法通道就绪后再点「启动」
  QTimer::singleShot(500, this, &MainWindow::TryAutoStartEngine);
}

void MainWindow::BindRecipeImportHandler(CameraManagerWidget* camera_manager) {
  if (camera_manager == nullptr) {
    return;
  }
  // 仅对下拉框选中的那一台相机下发配方，避免多工位互相覆盖。
  camera_manager->SetImportRecipeHandler(
      [this, camera_manager](const QString& camera_id, const QString& path) {
        if (!engine_ || camera_id.isEmpty()) {
          return;
        }
        const std::string id = camera_id.toStdString();
        auto cam = engine_->GetCamera(id);
        if (!cam || !cam->IsConnected()) {
          visual::EventBus::Instance().NotifyLog(
              visual::LogSeverity::kWarning,
              QStringLiteral("配方导入失败：相机 %1 未连接").arg(camera_id));
          statusBar()->showMessage(tr("配方导入失败：相机未连接"), 5000);
          return;
        }
        const std::string utf8_path = path.toUtf8().constData();
        visual::RecipeParamList params;
        if (!cam->LoadRecipeFile(utf8_path, &params)) {
          visual::EventBus::Instance().NotifyLog(
              visual::LogSeverity::kWarning,
              QStringLiteral("配方导入失败：相机 %1").arg(camera_id));
          statusBar()->showMessage(tr("配方导入失败"), 5000);
          return;
        }
        if (!params.empty()) {
          camera_manager->SetRecipeParams(params);
        }
        visual::EventBus::Instance().NotifyLog(
            QStringLiteral("配方导入成功：相机 %1").arg(camera_id));
        statusBar()->showMessage(tr("配方导入成功"), 3000);
      });
}

void MainWindow::OnOpen2DCameraImport() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("2D相机参数导入"));
  dlg.resize(520, 700);
  auto* layout = new QVBoxLayout(&dlg);
  auto* camera_manager = new CameraManagerWidget(&dlg);
  // 按序列号列出全部已配置相机，供导入时点选目标。
  std::vector<CameraChoice> choices;
  for (const auto& kv : visual::AppContext::Instance().Devices()) {
    CameraChoice c;
    c.camera_id = QString::fromStdString(kv.second.id);
    c.serial = QString::fromStdString(kv.second.serial);
    if (engine_) {
      auto cam = engine_->GetCamera(kv.second.id);
      c.connected = cam && cam->IsConnected();
    }
    choices.push_back(std::move(c));
  }
  camera_manager->SetCameraChoices(choices);
  BindRecipeImportHandler(camera_manager);
  layout->addWidget(camera_manager, 1);
  auto* close_btn = new QPushButton(tr("关闭"), &dlg);
  connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
  layout->addWidget(close_btn);
  dlg.exec();
}

void MainWindow::TryAutoStartEngine() {
  if (auto_start_done_ || engine_ == nullptr || engine_->IsRunning()) {
    auto_start_done_ = true;
    return;
  }
  if (offline_op_busy_) {
    QTimer::singleShot(400, this, &MainWindow::TryAutoStartEngine);
    return;
  }
  // 等算法进程真正可接单（通道已就绪）。仅进程拉起不够：首包会超时。
  if (visual::AppContext::Instance().Settings().use_shm_algo &&
      !visual::EventBus::IsAlgoProcessReady()) {
    if (auto_start_retries_ < 75) {  // ~30s
      ++auto_start_retries_;
      QTimer::singleShot(400, this, &MainWindow::TryAutoStartEngine);
      return;
    }
    visual::EventBus::Instance().NotifyLog(
        visual::LogSeverity::kWarning,
        QStringLiteral("等待算法就绪超时，仍尝试启动产线"));
  }
  auto_start_done_ = true;
  // 通道刚就绪后再略等一拍，避免与算法线程首轮初始化交错
  QTimer::singleShot(300, this, [this]() {
    if (engine_ && !engine_->IsRunning()) {
      OnStartEngine();
    }
  });
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (!layout_snapshot_.valid) {
    QTimer::singleShot(0, this, [this]() {
      QTimer::singleShot(50, this, [this]() { CaptureWindowLayoutSnapshot(); });
    });
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QString tip = tr("确定要退出 SmartGuide 吗？");
  if (engine_ && engine_->IsRunning()) {
    tip = tr("产线正在运行，确定要退出吗？\n退出前将停止产线并断开设备。");
  } else if (offline_op_busy_) {
    tip = tr("手动/回放任务进行中，确定要退出吗？\n将等待当前任务结束后退出。");
  }

  const auto ret = QMessageBox::question(this, tr("退出确认"), tip,
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ret != QMessageBox::Yes) {
    event->ignore();
    return;
  }
  event->accept();
  QMainWindow::closeEvent(event);
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
  layout_snapshot_.left_sizes = left_split_ != nullptr ? left_split_->sizes() : QList<int>{};
  layout_snapshot_.center_sizes = center_split_->sizes();
  layout_snapshot_.viewport_sizes = viewport_split_->sizes();
  layout_snapshot_.result_sizes = result_split_->sizes();
  layout_snapshot_.valid = layout_snapshot_.main_sizes.size() >= 2 &&
                           layout_snapshot_.center_sizes.size() >= 2 &&
                           layout_snapshot_.central_size.width() > 0 &&
                           layout_snapshot_.central_size.height() > 0;
}

void MainWindow::SetStationUiStatus(visual::StationId station, StationCycleUiStatus status) {
  QString text;
  switch (status) {
    case StationCycleUiStatus::kWorking:
      text = tr("工作中");
      break;
    case StationCycleUiStatus::kCompleted:
      text = tr("已完成");
      break;
    case StationCycleUiStatus::kFault:
      text = tr("检测异常");
      break;
    case StationCycleUiStatus::kIdle:
    default:
      text = tr("空闲");
      break;
  }
  if (QLabel* label = StationStatusLabelFor(station)) {
    const bool is_r09 = (station == visual::StationId::kR09);
    label->setText(is_r09 ? tr("R09: %1").arg(text) : tr("R05: %1").arg(text));
  }
}

void MainWindow::CancelStationIdleFallback(visual::StationId station) {
  if (QTimer* timer = StationIdleTimerFor(station)) {
    timer->stop();
  }
}

void MainWindow::ScheduleStationIdleFallback(visual::StationId station) {
  if (QTimer* timer = StationIdleTimerFor(station)) {
    timer->start(15000);
  }
}

QTimer* MainWindow::StationIdleTimerFor(visual::StationId station) const {
  return station == visual::StationId::kR09 ? station_r09_idle_timer_ : station_r05_idle_timer_;
}

QLabel* MainWindow::StationStatusLabelFor(visual::StationId station) const {
  return station == visual::StationId::kR09 ? station_r09_status_ : station_r05_status_;
}

void MainWindow::ResetStationCycleStatusLabels() {
  CancelStationIdleFallback(visual::StationId::kR05);
  CancelStationIdleFallback(visual::StationId::kR09);
  SetStationUiStatus(visual::StationId::kR05, StationCycleUiStatus::kIdle);
  SetStationUiStatus(visual::StationId::kR09, StationCycleUiStatus::kIdle);
}

void MainWindow::OnCycleStarted(visual::StationId station) {
  CancelStationIdleFallback(station);
  SetStationUiStatus(station, StationCycleUiStatus::kWorking);
}

void MainWindow::OnCycleCompleted(const visual::CycleResultEvent& event) {
  // 采图失败 / 算法失败 → 检测异常；否则已完成
  const StationCycleUiStatus status =
      event.algo_ok ? StationCycleUiStatus::kCompleted : StationCycleUiStatus::kFault;
  SetStationUiStatus(event.station, status);
  ScheduleStationIdleFallback(event.station);

  if (event.station == visual::StationId::kR09) {
    r09_table_->UpdateResults(event.logs);
    r09_viewport_->SetPreviewImage(event.image_bytes, event.image_width, event.image_height,
                                   event.image_format);
  } else {
    r05_table_->UpdateResults(event.logs);
    r05_viewport_->SetPreviewImage(event.image_bytes, event.image_width, event.image_height,
                                   event.image_format);
  }
  statusBar()->showMessage(tr("周期完成 station=%1 algo=%2 plc=%3")
                               .arg(static_cast<int>(event.station))
                               .arg(event.algo_ok)
                               .arg(event.plc_ok));
  // 熔断后 running_ 已 false 但 worker 可能仍在：UI 线程 Stop 完成 join/停心跳
  if (engine_ && stop_engine_button_ != nullptr && stop_engine_button_->isEnabled() &&
      !engine_->IsRunning()) {
    engine_->Stop();
    UpdateEngineControlState(false);
    ResetStationCycleStatusLabels();
  }
}

void MainWindow::OnLogLine(const QString& line) {
  log_view_->append(line);
  constexpr int kMaxLogBlocks = 2000;
  QTextDocument* doc = log_view_->document();
  while (doc != nullptr && doc->blockCount() > kMaxLogBlocks) {
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
  }
}

void MainWindow::OnStartEngine() {
  if (!engine_ || engine_->IsRunning()) {
    return;
  }
  if (offline_op_busy_) {
    statusBar()->showMessage(tr("手动或回放进行中，请等待结束后再启动产线"), 5000);
    return;
  }

  if (!simulation_mode_) {
    QString reason;
    if (!EnsureProductionDevicesReady(&reason)) {
      const QString msg = tr("设备状态异常，无法启动生产：%1").arg(reason);
      visual::EventBus::Instance().NotifyLog(msg);
      statusBar()->showMessage(msg, 8000);
      ApplyProductionStartInterlock();
      return;
    }
  }

  engine_->Start();
  engine_->ResetFaultBreakers();
  device_status_->SetPlcStatus(engine_->IsPlcConnected(), engine_->IsPlcConnected());
  ResetStationCycleStatusLabels();
  UpdateEngineControlState(true);
  visual::EventBus::Instance().NotifyLog(QStringLiteral("产线已启动"));
  statusBar()->showMessage(tr("产线已启动"));
}

void MainWindow::OnStopEngine() {
  if (!engine_ || !engine_->IsRunning()) {
    return;
  }
  engine_->Stop();
  device_status_->SetPlcStatus(engine_->IsPlcConnected(), engine_->IsPlcConnected());
  ResetStationCycleStatusLabels();
  UpdateEngineControlState(false);
  visual::EventBus::Instance().NotifyLog(QStringLiteral("产线已停止"));
  statusBar()->showMessage(tr("产线已停止"));
}

bool MainWindow::EnsureProductionDevicesReady(QString* reason) {
  auto set_reason = [&](const QString& text) {
    if (reason != nullptr) {
      *reason = text;
    }
  };

  const auto& settings = visual::AppContext::Instance().Settings();
  if (engine_ == nullptr) {
    set_reason(tr("主控未就绪"));
    return false;
  }

  // 刷新相机状态；允许部分 Offline（掉线工位产线内走快应答），不阻断启动
  QStringList offline_cams;
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
      offline_cams << QString::fromStdString(kv.second.id);
    }
  }
  if (!offline_cams.isEmpty()) {
    visual::EventBus::Instance().NotifyLog(
        visual::LogSeverity::kWarning,
        QStringLiteral("部分相机离线仍启动产线（离线工位将快速回复不合格）：%1")
            .arg(offline_cams.join(QStringLiteral(","))));
  }

  const bool require_algo_process = visual::AppContext::Instance().Settings().use_shm_algo;
  if (require_algo_process) {
    if (device_status_ == nullptr || !device_status_->IsAlgoOk()) {
      set_reason(tr("算法未运行"));
      return false;
    }
  }

  if (!engine_->TryConnectPlc()) {
    if (device_status_ != nullptr) {
      device_status_->SetPlcStatus(false, false);
    }
    set_reason(tr("产线控制器未连接"));
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

  QStringList faults;
  if (device_status_ != nullptr) {
    // 不再要求全部相机在线：Offline 工位由引擎快应答，健康工位照常跑
    if (visual::AppContext::Instance().Settings().use_shm_algo && !device_status_->IsAlgoOk()) {
      faults << tr("算法服务异常");
    }
  }
  const bool ready = faults.isEmpty();
  start_engine_button_->setEnabled(ready);
  QString tip;
  if (!ready) {
    tip = tr("设备状态异常，无法启动：%1").arg(faults.join(QStringLiteral("；")));
  } else if (device_status_ != nullptr && !device_status_->AreCamerasOk()) {
    tip = tr("部分相机离线：仍可启动，离线工位将快应答 PLC");
  }
  start_engine_button_->setToolTip(tip);
}

void MainWindow::UpdateEngineControlState(bool running) {
  if (start_engine_button_ != nullptr) {
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
  if (repeatability_action_ != nullptr) {
    repeatability_action_->setEnabled(allow);
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
    statusBar()->showMessage(tr("产线运行中，请先停止后再手动触发"), 5000);
    return;
  }

  JoinOfflineWorker();
  SetOfflineOpBusy(true);
  statusBar()->showMessage(tr("手动周期进行中…"));

  const auto engine = engine_;
  QPointer<MainWindow> self(this);
  offline_worker_ = std::thread([self, engine, station]() {
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
  });
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
    if (left_split_ != nullptr) {
      left_split_->setSizes(layout_snapshot_.left_sizes);
    }
    center_split_->setSizes(layout_snapshot_.center_sizes);
    viewport_split_->setSizes(layout_snapshot_.viewport_sizes);
    result_split_->setSizes(layout_snapshot_.result_sizes);
    return;
  }

  ApplyMainSplitReset(main_split_, layout_snapshot_);
  RefreshLayout(main_split_);

  ApplySavedSizes(left_split_, layout_snapshot_.left_sizes);
  ApplySavedSizes(center_split_, layout_snapshot_.center_sizes);
  RefreshLayout(center_split_);

  ApplySavedSizes(viewport_split_, layout_snapshot_.viewport_sizes);
  ApplySavedSizes(result_split_, layout_snapshot_.result_sizes);
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
    visual::EventBus::Instance().NotifyLog(visual::LogSeverity::kWarning,
                                           QStringLiteral("产线运行中，请先停止再做历史回放"));
    return;
  }
  const QString default_dir =
      QString::fromStdString(visual::ResolveDataRoot(visual::AppContext::Instance().Settings().data_path));
  const QString session_dir =
      QFileDialog::getExistingDirectory(this, tr("选择历史数据目录"), default_dir);
  if (session_dir.isEmpty()) {
    return;
  }

  JoinOfflineWorker();
  SetOfflineOpBusy(true);
  statusBar()->showMessage(tr("历史回放进行中…"));

  const auto engine = engine_;
  const std::string session = session_dir.toStdString();
  QPointer<MainWindow> self(this);
  offline_worker_ = std::thread([self, engine, station, session]() {
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
  });
}

void MainWindow::OnReplayR05() {
  RunHistoricalReplay(visual::StationId::kR05);
}

void MainWindow::OnReplayR09() {
  RunHistoricalReplay(visual::StationId::kR09);
}

void MainWindow::OnRepeatabilityTest() {
  if (!engine_) {
    return;
  }
  if (offline_op_busy_) {
    statusBar()->showMessage(tr("手动或回放进行中，请等待结束"), 5000);
    return;
  }
  if (engine_->IsRunning()) {
    statusBar()->showMessage(tr("产线运行中，请先停止后再做精度测试"), 5000);
    return;
  }
  auto* dlg = new RepeatabilityTestDialog(engine_, this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  connect(dlg, &RepeatabilityTestDialog::BusyChanged, this, &MainWindow::SetOfflineOpBusy);
  dlg->show();
}

/**
 * @file repeatability_test_dialog.h
 * @brief 重复精度测试独立窗口：实机连拍或离线扫深度文件，统计各 Log 数值极值。
 */
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <thread>

#include <QDialog>
#include <QString>
#include <QStringList>

#include "visual/event_bus.h"
#include "visual/sequence_engine.h"

class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTableWidget;
class QStackedWidget;

/** 单字段极值及产生该极值的深度文件路径。 */
struct RepeatabilityFieldExtrema {
  bool has_max = false;
  bool has_min = false;
  double max_v = 0;
  double min_v = 0;
  QString max_path;
  QString min_path;
};

/** 单条 Log（合格样本）上各数值字段的极值。 */
struct RepeatabilityLogExtrema {
  RepeatabilityFieldExtrema x;
  RepeatabilityFieldExtrema y;
  RepeatabilityFieldExtrema r;
  RepeatabilityFieldExtrema diameter;
  RepeatabilityFieldExtrema length;
};

/**
 * 配置并执行重复精度测试；不嵌入产线 Poll 路径。
 * 仅编排已有 RunOfflineCycle / RunReplayDepthFile，落盘规则与正常周期一致。
 */
class RepeatabilityTestDialog : public QDialog {
  Q_OBJECT
 public:
  explicit RepeatabilityTestDialog(std::shared_ptr<visual::SequenceEngine> engine,
                                   QWidget* parent = nullptr);
  ~RepeatabilityTestDialog() override;

 signals:
  /** 测试占用离线门禁时发出，供主窗口禁用手动/回放/启停。 */
  void BusyChanged(bool busy);

 private slots:
  void OnModeChanged(int index);
  void OnBrowseFolder();
  void OnStart();
  void OnStop();
  void OnProgress(int done, int total, const QString& detail);
  void OnFinished(bool ok, const QString& message);

 private:
  enum class Mode { kLive = 0, kOfflineFolder = 1 };

  void ApplyModeUi();
  void SetRunningUi(bool running);
  void RunWorker(Mode mode, visual::StationId station, int live_n, const QStringList& offline_files);
  void FillResultTable();
  QStringList CollectDepthFiles(const QString& root_dir) const;

  std::shared_ptr<visual::SequenceEngine> engine_;
  QComboBox* station_combo_ = nullptr;
  QComboBox* mode_combo_ = nullptr;
  QStackedWidget* mode_stack_ = nullptr;
  QSpinBox* live_count_spin_ = nullptr;
  QLineEdit* folder_edit_ = nullptr;
  QPushButton* browse_button_ = nullptr;
  QPushButton* start_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;
  QLabel* progress_label_ = nullptr;
  QTableWidget* result_table_ = nullptr;

  std::atomic<bool> cancel_{false};
  std::atomic<bool> running_{false};
  std::thread worker_;

  std::array<RepeatabilityLogExtrema, visual::kLogCountPerStation> extrema_{};
};

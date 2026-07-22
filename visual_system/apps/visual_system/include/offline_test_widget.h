#pragma once

#include <functional>

#include <QWidget>

class QPushButton;

class OfflineTestWidget : public QWidget {
  Q_OBJECT
 public:
  explicit OfflineTestWidget(QWidget* parent = nullptr);
  void SetRunHandler(std::function<void(int station)> handler);
  /** 产线运行中禁用，避免与 WorkerLoop 并发 RunCycle。 */
  void SetOfflineTestEnabled(bool enabled);
  /**
   * 手动/回放异步任务占用中：强制禁用按钮，防止连点入队多次周期。
   * 与 SetOfflineTestEnabled、工位 enabled 叠加：任一不允许则灰显。
   */
  void SetBusy(bool busy);
  /** 按 setting.json stations.*.enabled 刷新（与产线禁用、Busy 叠加）。 */
  void RefreshStationEnableFromSettings();

 private slots:
  void OnRunR05();
  void OnRunR09();

 private:
  void ApplyButtonEnableState();

  std::function<void(int station)> run_handler_;
  QPushButton* r05_button_ = nullptr;
  QPushButton* r09_button_ = nullptr;
  bool engine_allows_offline_ = true;
  bool busy_ = false;
};

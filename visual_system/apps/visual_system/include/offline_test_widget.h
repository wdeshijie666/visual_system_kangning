#pragma once

#include <functional>

#include <QWidget>

class QPushButton;

class OfflineTestWidget : public QWidget {
  Q_OBJECT
 public:
  explicit OfflineTestWidget(QWidget* parent = nullptr);
  void SetRunHandler(std::function<void(int station)> handler);
  /** 在线产线运行时禁用，避免 UI 线程与 WorkerLoop 并发 RunCycle。 */
  void SetOfflineTestEnabled(bool enabled);

 private slots:
  void OnRunR05();
  void OnRunR09();

 private:
  std::function<void(int station)> run_handler_;
  QPushButton* r05_button_ = nullptr;
  QPushButton* r09_button_ = nullptr;
};

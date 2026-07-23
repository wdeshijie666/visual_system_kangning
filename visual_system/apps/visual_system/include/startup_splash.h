/**
 * @file startup_splash.h
 * @brief 启动初始化等待界面（深色主题，与 dark.qss 一致）。
 */
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;

class StartupSplash : public QWidget {
 public:
  explicit StartupSplash(QWidget* parent = nullptr);

  void SetStatus(const QString& text);
  /** 刷新界面并处理事件，供阻塞式启动阶段调用。 */
  void Pump();

 private:
  QLabel* brand_ = nullptr;
  QLabel* status_ = nullptr;
  QProgressBar* bar_ = nullptr;
  QTimer* dots_timer_ = nullptr;
  QString status_base_;
  int dots_ = 0;
};

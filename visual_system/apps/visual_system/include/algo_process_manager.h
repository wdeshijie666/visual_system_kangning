#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVector>

#include "visual/run_mode.h"

/** 管理独立算法进程：与主程序同生命周期，崩溃自动拉起。 */class AlgoProcessManager : public QObject {
  Q_OBJECT
 public:
  explicit AlgoProcessManager(QObject* parent = nullptr);
  ~AlgoProcessManager() override;

  void Configure(const QString& program_dir, const QString& program_exe);
  void SetRunModeSync(bool simulation_mode, int image_width, int image_height,
                     const visual::SimulationResultProfile& algo_result);
  bool Start();
  void Stop();
  bool IsRunning() const;

 private slots:
  void OnProcessStarted();
  void OnProcessFinished(int exit_code, QProcess::ExitStatus status);
  void OnProcessError(QProcess::ProcessError error);
  void OnReadyReadStderr();
  void OnReadyReadStdout();
  void OnRestartTimer();

 private:
  QString ResolveExePath() const;
  QString ResolveWorkingDir() const;
  void LaunchProcess();
  void LogEvent(const QString& line);
  void ScheduleRestart(const QString& reason);
  bool ShouldRestart() const;
  void RecordRestartAttempt();
  void NotifyStatus(const QString& detail);
  void KillProcess();
  bool SyncAlgoConfigFile();

  QProcess process_;
  QTimer restart_timer_;
  QString program_dir_;
  QString program_exe_;
  bool simulation_mode_ = false;
  int simulation_image_width_ = 2040;
  int simulation_image_height_ = 1080;
  visual::SimulationResultProfile simulation_algo_result_{};
  bool intentional_stop_ = false;  bool restart_pending_ = false;
  QVector<qint64> restart_timestamps_ms_;
  static constexpr int kMaxRestartsPerMinute = 5;
  static constexpr int kRestartDelayMs = 2000;
};

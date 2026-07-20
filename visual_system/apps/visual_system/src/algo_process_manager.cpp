#include "algo_process_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <fstream>
#include <mutex>

#include "visual/algo_run_mode.h"
#include "visual/app_context.h"
#include "visual/event_bus.h"
#include "visual/run_mode.h"

namespace {

void AppendAlgoProcessFileLog(const QString& line) {
  static std::mutex log_mutex;
  std::lock_guard<std::mutex> lock(log_mutex);
  std::filesystem::create_directories("./logs");
  std::ofstream out("./logs/algo_process.log", std::ios::app);
  if (out.is_open()) {
    out << line.toStdString() << '\n';
  }
}

QString ProcessErrorText(QProcess::ProcessError error) {
  switch (error) {
    case QProcess::FailedToStart:
      return QStringLiteral("FailedToStart");
    case QProcess::Crashed:
      return QStringLiteral("Crashed");
    case QProcess::Timedout:
      return QStringLiteral("Timedout");
    case QProcess::WriteError:
      return QStringLiteral("WriteError");
    case QProcess::ReadError:
      return QStringLiteral("ReadError");
    case QProcess::UnknownError:
    default:
      return QStringLiteral("UnknownError");
  }
}

}  // namespace

AlgoProcessManager::AlgoProcessManager(QObject* parent) : QObject(parent) {
  restart_timer_.setSingleShot(true);
  connect(&restart_timer_, &QTimer::timeout, this, &AlgoProcessManager::OnRestartTimer);

  connect(&process_, &QProcess::started, this, &AlgoProcessManager::OnProcessStarted);
  connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &AlgoProcessManager::OnProcessFinished);
  connect(&process_, &QProcess::errorOccurred, this, &AlgoProcessManager::OnProcessError);
  connect(&process_, &QProcess::readyReadStandardOutput, this, &AlgoProcessManager::OnReadyReadStdout);
  connect(&process_, &QProcess::readyReadStandardError, this, &AlgoProcessManager::OnReadyReadStderr);
}

AlgoProcessManager::~AlgoProcessManager() {
  Stop();
}

void AlgoProcessManager::Configure(const QString& program_dir, const QString& program_exe) {
  program_dir_ = program_dir;
  program_exe_ = program_exe;
}

void AlgoProcessManager::SetRunModeSync(bool simulation_mode, int image_width, int image_height,
                                        const visual::SimulationResultProfile& algo_result) {
  simulation_mode_ = simulation_mode;
  simulation_image_width_ = image_width;
  simulation_image_height_ = image_height;
  simulation_algo_result_ = algo_result;
}

QString AlgoProcessManager::ResolveWorkingDir() const {
  const QString app_dir = QCoreApplication::applicationDirPath();
  QDir dir(app_dir);
  if (program_dir_.isEmpty()) {
    return app_dir;
  }
  if (QDir::isAbsolutePath(program_dir_)) {
    return QDir(program_dir_).absolutePath();
  }
  return dir.filePath(program_dir_);
}

QString AlgoProcessManager::ResolveExePath() const {
  const QString work_dir = ResolveWorkingDir();
  if (program_exe_.isEmpty()) {
    return QString();
  }
  return QDir(work_dir).filePath(program_exe_);
}

bool AlgoProcessManager::Start() {
  // 阶段 0.6：同步 algo_config.json 后 QProcess 拉起 mock_algo_service
  if (process_.state() != QProcess::NotRunning) {
    return IsRunning();
  }
  intentional_stop_ = false;
  restart_pending_ = false;
  restart_timestamps_ms_.clear();
  if (!SyncAlgoConfigFile()) {
    LogEvent(tr("算法配置同步失败，仍将尝试启动进程"));
  }
  LaunchProcess();
  return process_.state() != QProcess::NotRunning || restart_pending_;
}

bool AlgoProcessManager::SyncAlgoConfigFile() {
  const QString config_path = QDir(ResolveWorkingDir()).filePath(QStringLiteral("algo_config.json"));
  QFile file(config_path);

  QJsonObject root;
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly)) {
      LogEvent(tr("无法读取算法配置: %1").arg(QDir::toNativeSeparators(config_path)));
      return false;
    }
    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    file.close();
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      LogEvent(tr("算法配置 JSON 无效: %1").arg(QDir::toNativeSeparators(config_path)));
      return false;
    }
    root = doc.object();
  }

  int mode = root.value(QStringLiteral("mode")).toInt(visual::algo_config::kModeOnline);
  if (root.value(QStringLiteral("mode")).isString()) {
    const QString mode_text = root.value(QStringLiteral("mode")).toString();
    mode = (mode_text == QStringLiteral("offline_replay")) ? visual::algo_config::kModeOfflineReplay
                                                           : visual::algo_config::kModeOnline;
  }
  if (mode == visual::algo_config::kModeOfflineReplay) {
    root.insert(QStringLiteral("mode"), visual::algo_config::kModeOnline);
    LogEvent(tr("算法配置已从回放模式(%1)改为在线模式(%2)")
                 .arg(visual::algo_config::kModeOfflineReplay)
                 .arg(visual::algo_config::kModeOnline));
  }

  QJsonObject pipeline;
  pipeline.insert(QStringLiteral("enabled"), simulation_mode_);
  pipeline.insert(QStringLiteral("imageWidth"), simulation_image_width_);
  pipeline.insert(QStringLiteral("imageHeight"), simulation_image_height_);
  QJsonObject algo_result;
  algo_result.insert(QStringLiteral("status"), simulation_algo_result_.status);
  algo_result.insert(QStringLiteral("offsetX"), simulation_algo_result_.offset_x_mm);
  algo_result.insert(QStringLiteral("offsetY"), simulation_algo_result_.offset_y_mm);
  algo_result.insert(QStringLiteral("offsetR"), simulation_algo_result_.offset_r_deg);
  algo_result.insert(QStringLiteral("diameter"), simulation_algo_result_.diameter_mm);
  algo_result.insert(QStringLiteral("length"), simulation_algo_result_.length_mm);
  pipeline.insert(QStringLiteral("algoResult"), algo_result);
  root.insert(QStringLiteral("pipelineSimulation"), pipeline);
  root.insert(QStringLiteral("debugSaveDepth"),
              visual::AppContext::Instance().Settings().algo_debug_save_depth);
  root.insert(QStringLiteral("debugSavePointcloud"),
              visual::AppContext::Instance().Settings().algo_debug_save_pointcloud);

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    LogEvent(tr("无法写入算法配置: %1").arg(QDir::toNativeSeparators(config_path)));
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();

  LogEvent(simulation_mode_ ? tr("算法配置已同步为通路仿真模式") : tr("算法配置已同步为实机模式"));
  return true;
}

void AlgoProcessManager::Stop() {
  intentional_stop_ = true;
  restart_pending_ = false;
  restart_timer_.stop();
  KillProcess();
  NotifyStatus(tr("已停止"));
}

bool AlgoProcessManager::IsRunning() const {
  return process_.state() == QProcess::Running;
}

void AlgoProcessManager::LaunchProcess() {
  const QString exe_path = ResolveExePath();
  const QFileInfo exe_info(exe_path);
  if (!exe_info.exists() || !exe_info.isFile()) {
    const QString msg =
        tr("算法程序不存在: %1").arg(QDir::toNativeSeparators(exe_path));
    LogEvent(msg);
    NotifyStatus(msg);
    return;
  }

  const QString work_dir = ResolveWorkingDir();
  process_.setProgram(exe_path);
  process_.setWorkingDirectory(work_dir);
  process_.setProcessChannelMode(QProcess::SeparateChannels);

  LogEvent(tr("启动算法进程: %1").arg(QDir::toNativeSeparators(exe_path)));
  NotifyStatus(tr("启动中"));

  process_.start();
  if (!process_.waitForStarted(5000)) {
    const QString msg =
        tr("算法进程启动失败: %1 (%2)")
            .arg(QDir::toNativeSeparators(exe_path), process_.errorString());
    LogEvent(msg);
    NotifyStatus(msg);
    if (!intentional_stop_ && ShouldRestart()) {
      ScheduleRestart(tr("启动失败"));
    }
  }
}

void AlgoProcessManager::KillProcess() {
  if (process_.state() == QProcess::NotRunning) {
    return;
  }
  process_.terminate();
  if (!process_.waitForFinished(3000)) {
    process_.kill();
    process_.waitForFinished(1000);
  }
}

void AlgoProcessManager::OnProcessStarted() {
  const QString msg = tr("算法进程已启动 (pid=%1)").arg(process_.processId());
  LogEvent(msg);
  NotifyStatus(tr("运行中"));
}

void AlgoProcessManager::OnProcessFinished(int exit_code, QProcess::ExitStatus status) {
  if (intentional_stop_) {
    LogEvent(tr("算法进程已退出 (intentional, exit=%1)").arg(exit_code));
    NotifyStatus(tr("已停止"));
    return;
  }

  QString reason;
  if (status == QProcess::CrashExit) {
    reason = tr("算法进程崩溃 (exit=%1)").arg(exit_code);
  } else {
    reason = tr("算法进程异常退出 (exit=%1)").arg(exit_code);
  }
  LogEvent(reason);
  NotifyStatus(reason);
  ScheduleRestart(reason);
}

void AlgoProcessManager::OnProcessError(QProcess::ProcessError error) {
  if (intentional_stop_) {
    return;
  }
  const QString msg =
      tr("算法进程错误: %1 (%2)").arg(ProcessErrorText(error), process_.errorString());
  LogEvent(msg);
  NotifyStatus(msg);
  if (error == QProcess::FailedToStart && process_.state() == QProcess::NotRunning) {
    ScheduleRestart(msg);
  }
}

void AlgoProcessManager::OnReadyReadStdout() {
  const QString text = QString::fromLocal8Bit(process_.readAllStandardOutput()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    LogEvent(QStringLiteral("[algo stdout] %1").arg(line));
  }
}

void AlgoProcessManager::OnReadyReadStderr() {
  const QString text = QString::fromLocal8Bit(process_.readAllStandardError()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    LogEvent(QStringLiteral("[algo stderr] %1").arg(line));
  }
}

bool AlgoProcessManager::ShouldRestart() const {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  int recent = 0;
  for (qint64 ts : restart_timestamps_ms_) {
    if (now - ts < 60000) {
      ++recent;
    }
  }
  return recent < kMaxRestartsPerMinute;
}

void AlgoProcessManager::RecordRestartAttempt() {
  restart_timestamps_ms_.append(QDateTime::currentMSecsSinceEpoch());
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  QVector<qint64> kept;
  kept.reserve(restart_timestamps_ms_.size());
  for (qint64 ts : restart_timestamps_ms_) {
    if (now - ts < 60000) {
      kept.append(ts);
    }
  }
  restart_timestamps_ms_ = std::move(kept);
}

void AlgoProcessManager::ScheduleRestart(const QString& reason) {
  if (intentional_stop_ || restart_pending_) {
    return;
  }
  if (!ShouldRestart()) {
    const QString msg = tr("算法进程重启次数过多，已停止自动拉起: %1").arg(reason);
    LogEvent(msg);
    NotifyStatus(msg);
    return;
  }

  RecordRestartAttempt();
  restart_pending_ = true;
  const QString msg = tr("将在 %1 ms 后重启算法进程: %2").arg(kRestartDelayMs).arg(reason);
  LogEvent(msg);
  NotifyStatus(tr("等待重启"));
  restart_timer_.start(kRestartDelayMs);
}

void AlgoProcessManager::OnRestartTimer() {
  restart_pending_ = false;
  if (intentional_stop_) {
    return;
  }
  LaunchProcess();
}

void AlgoProcessManager::LogEvent(const QString& line) {
  const QString stamped =
      QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"), line);
  AppendAlgoProcessFileLog(stamped);
  visual::EventBus::Instance().NotifyLog(stamped);
}

void AlgoProcessManager::NotifyStatus(const QString& detail) {
  visual::EventBus::Instance().NotifyAlgoProcessStatus(IsRunning(), detail);
}

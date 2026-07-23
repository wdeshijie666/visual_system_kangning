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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#endif

#include "visual/algo_run_mode.h"
#include "visual/app_context.h"
#include "visual/event_bus.h"
#include "visual/log_format.h"
#include "visual/rotating_file_log.h"
#include "visual/run_mode.h"

namespace {

visual::RotatingFileLog& AlgoFileLog() {
  static visual::RotatingFileLog log("./logs/algo_process.log", 8 * 1024 * 1024, 9);
  return log;
}

void AppendAlgoProcessFileLog(const QString& line) {
  AlgoFileLog().Append(line.toStdString());
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

  // SHM 超时等场景：杀进程后走现有自动拉起逻辑
  connect(&visual::EventBus::Instance(), &visual::EventBus::RequestAlgoRestart, this,
          [this](const QString& reason) {
            LogEvent(tr("收到算法重启请求: %1").arg(reason));
            NotifyStatus(tr("超时重启中"));
            KillProcess();
            if (!intentional_stop_) {
              ScheduleRestart(reason);
            }
          });
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
  external_running_ = false;
  restart_timestamps_ms_.clear();
  if (!SyncAlgoConfigFile()) {
    LogEvent(tr("算法配置同步失败，仍将尝试启动进程"));
  }
  // 已有同名进程在跑：避免双开抢 SHM，直接认定就绪
  if (IsAlgoExeAlreadyRunning()) {
    external_running_ = true;
    LogEvent(tr("检测到算法进程已在运行，跳过重复拉起"));
    NotifyStatus(tr("外部已运行"));
    return true;
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
  root.insert(QStringLiteral("transferDepth"),
              visual::AppContext::Instance().Settings().algo_transfer_depth);
  root.insert(QStringLiteral("transferPointcloud"),
              visual::AppContext::Instance().Settings().algo_transfer_pointcloud);
  // 保留真实算法开关（若文件已有则不强制改写；缺省写 true）
  if (!root.contains(QStringLiteral("usePointCloudAlgo"))) {
    root.insert(QStringLiteral("usePointCloudAlgo"), true);
  }
  if (!root.contains(QStringLiteral("pointCloudConfig"))) {
    root.insert(QStringLiteral("pointCloudConfig"), QStringLiteral("config.json"));
  }
  if (!root.contains(QStringLiteral("pointCloudTopN"))) {
    root.insert(QStringLiteral("pointCloudTopN"), 5);
  }

  // 将视觉侧双通道名同步给算法进程，保证映射名一致
  const auto& app_settings = visual::AppContext::Instance().Settings();
  QJsonObject channels;
  QJsonObject ch_r05;
  ch_r05.insert(QStringLiteral("enabled"), app_settings.station_r05.enabled);
  ch_r05.insert(QStringLiteral("shmName"), QString::fromStdString(app_settings.algo_channel_r05.shm_name));
  ch_r05.insert(QStringLiteral("mutexName"),
                QString::fromStdString(app_settings.algo_channel_r05.mutex_name));
  QJsonObject ch_r09;
  ch_r09.insert(QStringLiteral("enabled"), app_settings.station_r09.enabled);
  ch_r09.insert(QStringLiteral("shmName"), QString::fromStdString(app_settings.algo_channel_r09.shm_name));
  ch_r09.insert(QStringLiteral("mutexName"),
                QString::fromStdString(app_settings.algo_channel_r09.mutex_name));
  channels.insert(QStringLiteral("r05"), ch_r05);
  channels.insert(QStringLiteral("r09"), ch_r09);
  root.insert(QStringLiteral("channels"), channels);

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
  external_running_ = false;
  NotifyStatus(tr("已停止"));
}

bool AlgoProcessManager::IsRunning() const {
  if (process_.state() == QProcess::Running) {
    return true;
  }
  // 外部进程：每次查询时复核，退出后清状态
  if (external_running_) {
    return IsAlgoExeAlreadyRunning();
  }
  return false;
}

bool AlgoProcessManager::IsAlgoExeAlreadyRunning() const {
#ifdef _WIN32
  const QString want = QFileInfo(ResolveExePath()).fileName();
  if (want.isEmpty()) {
    return false;
  }
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return false;
  }
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  bool found = false;
  const DWORD self_pid = GetCurrentProcessId();
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == self_pid) {
        continue;
      }
      const QString name = QString::fromWCharArray(pe.szExeFile);
      if (QString::compare(name, want, Qt::CaseInsensitive) == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
#else
  return false;
#endif
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
  // 算法进程以 /utf-8 编译，stdout 为 UTF-8；不可用 fromLocal8Bit（会按系统 ANSI 解导致中文乱码）
  const QString text = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    LogEvent(QStringLiteral("[algo] %1").arg(line));
  }
}

void AlgoProcessManager::OnReadyReadStderr() {
  const QString text = QString::fromUtf8(process_.readAllStandardError()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    LogEvent(QStringLiteral("[algo] %1").arg(line));
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
  // EventBus / 文件日志统一由 NotifyLog 加 [info][时间] 前缀，避免双重时间戳
  visual::EventBus::Instance().NotifyLog(visual::LogSeverity::kInfo, line);
  const QString stamped =
      QString::fromStdString(visual::FormatLogLine(visual::LogSeverity::kInfo, line.toStdString()));
  AppendAlgoProcessFileLog(stamped);
}

void AlgoProcessManager::NotifyStatus(const QString& detail) {
  // 外部进程可能已退出：复核后再上报
  if (external_running_ && process_.state() != QProcess::Running) {
    external_running_ = IsAlgoExeAlreadyRunning();
  }
  visual::EventBus::Instance().NotifyAlgoProcessStatus(IsRunning(), detail);
}

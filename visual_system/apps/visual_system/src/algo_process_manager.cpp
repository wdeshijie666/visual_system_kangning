#include "algo_process_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>

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
            LogEvent(tr("收到算法重启请求"));
            NotifyStatus(tr("超时重启中"), false);
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
  channels_ready_count_ = 0;
  service_ready_notified_ = false;
  restart_timestamps_ms_.clear();
  if (!SyncAlgoConfigFile()) {
    LogEvent(tr("算法配置同步失败，仍将尝试启动算法"));
  }
  // 残留/外部算法进程仍持有启动时读入的旧配置；必须先杀掉再拉起，否则改 pointCloudConfig 不生效
  if (IsAlgoExeAlreadyRunning()) {
    LogEvent(tr("发现已有算法在运行，将按最新配置重新启动"));
    KillExternalAlgoProcesses();
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
      LogEvent(tr("无法读取算法配置文件"));
      return false;
    }
    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    file.close();
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      LogEvent(tr("算法配置文件内容无效"));
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
    LogEvent(tr("算法配置已改为在线计算模式"));
  }

  QJsonObject pipeline;
  // runMode=simulation 只驱动虚拟相机/Memory PLC；任一工位启用点云算法时禁用通路假结果。
  const QJsonObject existing_channels = root.value(QStringLiteral("channels")).toObject();
  const auto channel_uses_pc = [&](const QString& key) -> bool {
    const QJsonObject ch = existing_channels.value(key).toObject();
    if (ch.contains(QStringLiteral("usePointCloudAlgo"))) {
      return ch.value(QStringLiteral("usePointCloudAlgo")).toBool(true);
    }
    if (root.contains(QStringLiteral("usePointCloudAlgo"))) {
      return root.value(QStringLiteral("usePointCloudAlgo")).toBool(true);
    }
    return true;
  };
  const bool use_pc_algo = channel_uses_pc(QStringLiteral("r05")) || channel_uses_pc(QStringLiteral("r09"));
  pipeline.insert(QStringLiteral("enabled"), simulation_mode_ && !use_pc_algo);
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
  root.insert(QStringLiteral("transferGray"),
              visual::AppContext::Instance().Settings().algo_transfer_gray);

  // 顶层三项仅作兼容默认；正式按工位写在 channels.* 下
  const bool default_use_pc =
      root.contains(QStringLiteral("usePointCloudAlgo"))
          ? root.value(QStringLiteral("usePointCloudAlgo")).toBool(true)
          : true;
  const QString default_pc_cfg =
      root.contains(QStringLiteral("pointCloudConfig"))
          ? root.value(QStringLiteral("pointCloudConfig")).toString(QStringLiteral("config.json"))
          : QStringLiteral("config.json");
  const int default_top_n =
      root.contains(QStringLiteral("pointCloudTopN"))
          ? root.value(QStringLiteral("pointCloudTopN")).toInt(5)
          : 5;
  if (!root.contains(QStringLiteral("usePointCloudAlgo"))) {
    root.insert(QStringLiteral("usePointCloudAlgo"), default_use_pc);
  }
  if (!root.contains(QStringLiteral("pointCloudConfig"))) {
    root.insert(QStringLiteral("pointCloudConfig"), default_pc_cfg);
  }
  if (!root.contains(QStringLiteral("pointCloudTopN"))) {
    root.insert(QStringLiteral("pointCloudTopN"), default_top_n);
  }
  // 两工位共用参考点；已有现场值则保留
  if (!root.contains(QStringLiteral("referencePointConfig"))) {
    root.insert(QStringLiteral("referencePointConfig"), QStringLiteral("reference_point.json"));
  }

  const auto merge_channel_point_cloud = [&](QJsonObject* ch, const QString& key) {
    if (ch == nullptr) {
      return;
    }
    const QJsonObject prev = existing_channels.value(key).toObject();
    const bool use_pc = prev.contains(QStringLiteral("usePointCloudAlgo"))
                            ? prev.value(QStringLiteral("usePointCloudAlgo")).toBool(default_use_pc)
                            : default_use_pc;
    const QString pc_cfg = prev.contains(QStringLiteral("pointCloudConfig"))
                               ? prev.value(QStringLiteral("pointCloudConfig")).toString(default_pc_cfg)
                               : default_pc_cfg;
    const int top_n = prev.contains(QStringLiteral("pointCloudTopN"))
                          ? prev.value(QStringLiteral("pointCloudTopN")).toInt(default_top_n)
                          : default_top_n;
    ch->insert(QStringLiteral("usePointCloudAlgo"), use_pc);
    ch->insert(QStringLiteral("pointCloudConfig"), pc_cfg);
    ch->insert(QStringLiteral("pointCloudTopN"), top_n > 0 ? top_n : 5);
  };

  // 将视觉侧双通道名同步给算法进程，并保留/补齐每工位点云参数
  const auto& app_settings = visual::AppContext::Instance().Settings();
  QJsonObject channels;
  QJsonObject ch_r05;
  ch_r05.insert(QStringLiteral("enabled"), app_settings.station_r05.enabled);
  ch_r05.insert(QStringLiteral("shmName"), QString::fromStdString(app_settings.algo_channel_r05.shm_name));
  ch_r05.insert(QStringLiteral("mutexName"),
                QString::fromStdString(app_settings.algo_channel_r05.mutex_name));
  merge_channel_point_cloud(&ch_r05, QStringLiteral("r05"));
  QJsonObject ch_r09;
  ch_r09.insert(QStringLiteral("enabled"), app_settings.station_r09.enabled);
  ch_r09.insert(QStringLiteral("shmName"), QString::fromStdString(app_settings.algo_channel_r09.shm_name));
  ch_r09.insert(QStringLiteral("mutexName"),
                QString::fromStdString(app_settings.algo_channel_r09.mutex_name));
  merge_channel_point_cloud(&ch_r09, QStringLiteral("r09"));
  channels.insert(QStringLiteral("r05"), ch_r05);
  channels.insert(QStringLiteral("r09"), ch_r09);
  root.insert(QStringLiteral("channels"), channels);

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    LogEvent(tr("无法保存算法配置文件"));
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();

  LogEvent((simulation_mode_ && !use_pc_algo) ? tr("算法配置已按仿真方式准备就绪")
                                              : tr("算法配置已按正式计算准备就绪"));
  return true;
}

void AlgoProcessManager::Stop() {
  intentional_stop_ = true;
  restart_pending_ = false;
  restart_timer_.stop();
  KillProcess();
  KillExternalAlgoProcesses();
  external_running_ = false;
  channels_ready_count_ = 0;
  service_ready_notified_ = false;
  NotifyStatus(tr("已停止"), false);
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
    const QString msg = tr("找不到算法程序，请检查安装目录");
    LogEvent(msg);
    NotifyStatus(msg, false);
    return;
  }

  const QString work_dir = ResolveWorkingDir();
  process_.setProgram(exe_path);
  process_.setWorkingDirectory(work_dir);
  process_.setProcessChannelMode(QProcess::SeparateChannels);

  channels_ready_count_ = 0;
  service_ready_notified_ = false;
  LogEvent(tr("正在启动算法"));
  NotifyStatus(tr("启动中"), false);

  process_.start();
  if (!process_.waitForStarted(5000)) {
    const QString msg = tr("算法启动失败：%1").arg(process_.errorString());
    LogEvent(msg);
    NotifyStatus(msg, false);
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

void AlgoProcessManager::KillExternalAlgoProcesses() {
#ifdef _WIN32
  const QString want = QFileInfo(ResolveExePath()).fileName();
  if (want.isEmpty()) {
    return;
  }
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return;
  }
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  const DWORD self_pid = GetCurrentProcessId();
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == self_pid) {
        continue;
      }
      const QString name = QString::fromWCharArray(pe.szExeFile);
      if (QString::compare(name, want, Qt::CaseInsensitive) != 0) {
        continue;
      }
      HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
      if (proc == nullptr) {
        continue;
      }
      LogEvent(tr("已结束旧的算法程序"));
      TerminateProcess(proc, 1);
      WaitForSingleObject(proc, 3000);
      CloseHandle(proc);
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  // 给系统一点时间释放 SHM/互斥体句柄
  QThread::msleep(200);
#else
  (void)0;
#endif
}

void AlgoProcessManager::OnProcessStarted() {
  channels_ready_count_ = 0;
  service_ready_notified_ = false;
  const QString msg = tr("算法已启动，等待就绪");
  LogEvent(msg);
  // 进程在跑但通道尚未监听：alive=true、ready=false，避免首包竞态
  NotifyStatus(tr("启动中"), false);
}

void AlgoProcessManager::OnProcessFinished(int exit_code, QProcess::ExitStatus status) {
  channels_ready_count_ = 0;
  service_ready_notified_ = false;
  if (intentional_stop_) {
    LogEvent(tr("算法已正常退出"));
    NotifyStatus(tr("已停止"), false);
    return;
  }

  QString reason;
  if (status == QProcess::CrashExit) {
    reason = tr("算法异常退出，准备重启");
  } else {
    reason = tr("算法意外退出，准备重启");
  }
  (void)exit_code;
  LogEvent(reason);
  NotifyStatus(reason, false);
  ScheduleRestart(reason);
}

void AlgoProcessManager::OnProcessError(QProcess::ProcessError error) {
  if (intentional_stop_) {
    return;
  }
  const QString msg = tr("算法运行出错：%1").arg(process_.errorString());
  LogEvent(msg);
  NotifyStatus(msg, false);
  if (error == QProcess::FailedToStart && process_.state() == QProcess::NotRunning) {
    ScheduleRestart(msg);
  }
}

namespace {
/** 去掉算法子进程自带的 [级别][时间] 前缀，避免界面双重时间戳。 */
QString StripAlgoStamp(const QString& line) {
  static const QRegularExpression kStamp(
      QStringLiteral(R"(^\[(?:info|warning|debug|error)\]\s*\[[^\]]+\]\s*)"));
  QString out = line;
  out.remove(kStamp);
  return out.trimmed();
}
}  // namespace

void AlgoProcessManager::OnReadyReadStdout() {
  // 算法进程以 /utf-8 编译，stdout 为 UTF-8；不可用 fromLocal8Bit（会按系统 ANSI 解导致中文乱码）
  const QString text = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    const QString trimmed = StripAlgoStamp(line.trimmed());
    if (trimmed.isEmpty()) {
      continue;
    }
    LogEvent(QStringLiteral("算法：%1").arg(trimmed));
    NoteAlgoLogLine(trimmed);
  }
}

void AlgoProcessManager::OnReadyReadStderr() {
  const QString text = QString::fromUtf8(process_.readAllStandardError()).trimmed();
  if (text.isEmpty()) {
    return;
  }
  for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
    const QString trimmed = StripAlgoStamp(line.trimmed());
    if (trimmed.isEmpty()) {
      continue;
    }
    LogEvent(QStringLiteral("算法：%1").arg(trimmed));
    NoteAlgoLogLine(trimmed);
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
    const QString msg = tr("算法短时间多次退出，已停止自动重启");
    LogEvent(msg);
    NotifyStatus(msg, false);
    return;
  }

  RecordRestartAttempt();
  restart_pending_ = true;
  const QString msg = tr("约 %1 秒后自动重启算法").arg(kRestartDelayMs / 1000);
  LogEvent(msg);
  NotifyStatus(tr("等待重启"), false);
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

void AlgoProcessManager::NotifyStatus(const QString& detail, bool service_ready) {
  // 外部进程可能已退出：复核后再上报
  if (external_running_ && process_.state() != QProcess::Running) {
    external_running_ = IsAlgoExeAlreadyRunning();
  }
  visual::EventBus::Instance().NotifyAlgoProcessStatus(IsRunning(), detail, service_ready);
}

void AlgoProcessManager::NoteAlgoLogLine(const QString& line) {
  if (service_ready_notified_ || !IsRunning()) {
    return;
  }
  // ASCII 标记优先（避免编码导致中文匹配失败）；中文文案作兼容
  if (!line.contains(QLatin1String("CHANNEL_READY")) &&
      !line.contains(QStringLiteral("通道已就绪"))) {
    return;
  }
  ++channels_ready_count_;
  if (channels_ready_count_ >= 1 && !service_ready_notified_) {
    service_ready_notified_ = true;
    LogEvent(tr("算法已就绪，可以接收计算任务"));
    NotifyStatus(tr("运行中"), true);
  }
}

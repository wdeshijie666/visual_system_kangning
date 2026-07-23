/**
 * @file event_bus.cpp
 * @brief 事件总线：后台线程 Notify* 一律排队到 EventBus 所属线程再 emit，避免跨线程直连 UI 崩溃。
 */
#include "visual/event_bus.h"

#include <atomic>

#include <QMetaObject>
#include <QThread>

#include "visual/algo_liveness.h"
#include "visual/log_format.h"
#include "visual/rotating_file_log.h"

namespace visual {
namespace {

RotatingFileLog& AppFileLog() {
  // 当前 + .1.bak … .9.bak，最多 10×8MB
  static RotatingFileLog log("./logs/visual_system.log", 8 * 1024 * 1024, 9);
  return log;
}

std::atomic<bool>& AlgoReadyFlag() {
  static std::atomic<bool> ready{false};
  return ready;
}

void AppendUtf8Log(const QString& line) {
  const QByteArray utf8 = line.toUtf8();
  AppFileLog().Append(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

}  // namespace

EventBus& EventBus::Instance() {
  static EventBus bus;
  return bus;
}

bool EventBus::IsAlgoProcessReady() {
  return AlgoReadyFlag().load();
}

void EventBus::NotifyPlcStatus(bool connected, bool heartbeat) {
  if (QThread::currentThread() != thread()) {
    const bool c = connected;
    const bool h = heartbeat;
    QMetaObject::invokeMethod(
        this, [this, c, h]() { NotifyPlcStatus(c, h); }, Qt::QueuedConnection);
    return;
  }
  emit PlcStatusChanged(connected, heartbeat);
}

void EventBus::NotifyCameraStatus(const QString& camera_id, bool connected) {
  if (QThread::currentThread() != thread()) {
    const QString id = camera_id;
    const bool c = connected;
    QMetaObject::invokeMethod(
        this, [this, id, c]() { NotifyCameraStatus(id, c); }, Qt::QueuedConnection);
    return;
  }
  emit CameraStatusChanged(camera_id, connected);
}

void EventBus::NotifyTrigger(StationId station) {
  if (QThread::currentThread() != thread()) {
    const StationId s = station;
    QMetaObject::invokeMethod(this, [this, s]() { NotifyTrigger(s); }, Qt::QueuedConnection);
    return;
  }
  emit TriggerReceived(station);
}

void EventBus::NotifyCycleStarted(StationId station) {
  if (QThread::currentThread() != thread()) {
    const StationId s = station;
    QMetaObject::invokeMethod(this, [this, s]() { NotifyCycleStarted(s); }, Qt::QueuedConnection);
    return;
  }
  emit CycleStarted(station);
}

void EventBus::NotifyCycleCompleted(const CycleResultEvent& event) {
  if (QThread::currentThread() != thread()) {
    const CycleResultEvent ev = event;
    QMetaObject::invokeMethod(
        this, [this, ev]() { NotifyCycleCompleted(ev); }, Qt::QueuedConnection);
    return;
  }
  emit CycleCompleted(event);
}

void EventBus::NotifyLog(const QString& line) {
  NotifyLog(LogSeverity::kInfo, line);
}

void EventBus::NotifyLog(LogSeverity level, const QString& line) {
  if (QThread::currentThread() != thread()) {
    const LogSeverity lv = level;
    const QString copy = line;
    QMetaObject::invokeMethod(
        this, [this, lv, copy]() { NotifyLog(lv, copy); }, Qt::QueuedConnection);
    return;
  }
  const QString formatted =
      QString::fromStdString(FormatLogLine(level, line.toStdString()));
  AppendUtf8Log(formatted);
  emit LogLine(formatted);
}

void EventBus::NotifyAlgoProcessStatus(bool running, const QString& detail, bool service_ready) {
  if (QThread::currentThread() != thread()) {
    const bool r = running;
    const QString d = detail;
    const bool sr = service_ready;
    QMetaObject::invokeMethod(
        this, [this, r, d, sr]() { NotifyAlgoProcessStatus(r, d, sr); }, Qt::QueuedConnection);
    return;
  }
  AlgoReadyFlag().store(running && service_ready);
  SetAlgoProcessAlive(running);
  emit AlgoProcessStatusChanged(running && service_ready, detail);
}

void EventBus::NotifyRequestAlgoRestart(const QString& reason) {
  if (QThread::currentThread() != thread()) {
    const QString r = reason;
    QMetaObject::invokeMethod(
        this, [this, r]() { NotifyRequestAlgoRestart(r); }, Qt::QueuedConnection);
    return;
  }
  AlgoReadyFlag().store(false);
  SetAlgoProcessAlive(false);
  emit RequestAlgoRestart(reason);
}

}  // namespace visual

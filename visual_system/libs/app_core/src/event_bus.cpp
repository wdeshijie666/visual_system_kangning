/**
 * @file event_bus.cpp
 * @brief 事件总线：后台线程 Notify* 一律排队到 EventBus 所属线程再 emit，避免跨线程直连 UI 崩溃。
 */
#include "visual/event_bus.h"

#include <atomic>

#include <QMetaObject>
#include <QThread>

#include "visual/rotating_file_log.h"

namespace visual {
namespace {

RotatingFileLog& AppFileLog() {
  static RotatingFileLog log("./logs/visual_system.log", 8 * 1024 * 1024);
  return log;
}

std::atomic<bool>& AlgoReadyFlag() {
  static std::atomic<bool> ready{false};
  return ready;
}

/** 文件日志统一写 UTF-8，避免 Windows 本地代码页导致中文乱码。 */
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
  if (QThread::currentThread() != thread()) {
    const QString copy = line;
    QMetaObject::invokeMethod(this, [this, copy]() { NotifyLog(copy); }, Qt::QueuedConnection);
    return;
  }
  AppendUtf8Log(line);
  emit LogLine(line);
}

void EventBus::NotifyAlgoProcessStatus(bool running, const QString& detail) {
  if (QThread::currentThread() != thread()) {
    const bool r = running;
    const QString d = detail;
    QMetaObject::invokeMethod(
        this, [this, r, d]() { NotifyAlgoProcessStatus(r, d); }, Qt::QueuedConnection);
    return;
  }
  AlgoReadyFlag().store(running);
  emit AlgoProcessStatusChanged(running, detail);
}

void EventBus::NotifyRequestAlgoRestart(const QString& reason) {
  if (QThread::currentThread() != thread()) {
    const QString r = reason;
    QMetaObject::invokeMethod(
        this, [this, r]() { NotifyRequestAlgoRestart(r); }, Qt::QueuedConnection);
    return;
  }
  AlgoReadyFlag().store(false);
  emit RequestAlgoRestart(reason);
}

}  // namespace visual

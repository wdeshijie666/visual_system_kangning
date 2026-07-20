#include "visual/event_bus.h"

#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace visual {
namespace {

void AppendFileLog(const QString& line) {
  static std::mutex log_mutex;
  std::lock_guard<std::mutex> lock(log_mutex);
  fs::create_directories("./logs");
  std::ofstream out("./logs/visual_system.log", std::ios::app);
  if (out.is_open()) {
    out << line.toStdString() << '\n';
  }
}

}  // namespace

EventBus& EventBus::Instance() {
  static EventBus bus;
  return bus;
}

void EventBus::NotifyPlcStatus(bool connected, bool heartbeat) {
  emit PlcStatusChanged(connected, heartbeat);
}

void EventBus::NotifyCameraStatus(const QString& camera_id, bool connected) {
  emit CameraStatusChanged(camera_id, connected);
}

void EventBus::NotifyTrigger(StationId station) {
  emit TriggerReceived(station);
}

void EventBus::NotifyCycleCompleted(const CycleResultEvent& event) {
  emit CycleCompleted(event);
}

void EventBus::NotifyLog(const QString& line) {
  AppendFileLog(line);
  emit LogLine(line);
}

void EventBus::NotifyAlgoProcessStatus(bool running, const QString& detail) {
  emit AlgoProcessStatusChanged(running, detail);
}

}  // namespace visual

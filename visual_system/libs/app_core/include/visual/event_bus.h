/**
 * @file event_bus.h
 * @brief UI 与后台解耦的全局事件总线。
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "visual/station_types.h"

namespace visual {

struct CycleResultEvent {
  StationId station = StationId::kR05;
  LogResultBatch logs{};
  QString session_dir;
  /** 工位预览用 Mono8 灰度（内存，不落盘）。 */
  QByteArray gray_bytes;
  int gray_width = 0;
  int gray_height = 0;
  bool plc_ok = false;
  bool algo_ok = false;
};

class EventBus : public QObject {
  Q_OBJECT
 public:
  static EventBus& Instance();

  void NotifyPlcStatus(bool connected, bool heartbeat);
  void NotifyCameraStatus(const QString& camera_id, bool connected);
  void NotifyTrigger(StationId station);
  void NotifyCycleCompleted(const CycleResultEvent& event);
  void NotifyLog(const QString& line);
  void NotifyAlgoProcessStatus(bool running, const QString& detail);
  /** 请求重启算法进程（如 SHM 超时挂死）。 */
  void NotifyRequestAlgoRestart(const QString& reason);

  /** 独立算法进程是否处于可服务状态（无进程 Mock 时恒为 true）。 */
  static bool IsAlgoProcessReady();

 signals:
  void PlcStatusChanged(bool connected, bool heartbeat);
  void CameraStatusChanged(const QString& camera_id, bool connected);
  void TriggerReceived(visual::StationId station);
  void CycleCompleted(const visual::CycleResultEvent& event);
  void LogLine(const QString& line);
  void AlgoProcessStatusChanged(bool running, const QString& detail);
  void RequestAlgoRestart(const QString& reason);

 private:
  EventBus() = default;
};

}  // namespace visual

Q_DECLARE_METATYPE(visual::StationId)
Q_DECLARE_METATYPE(visual::CycleResultEvent)

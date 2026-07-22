/**
 * @file alarm_service.h
 * @brief 工业报警服务：分级、历史、落盘、UI 通知（可复用）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <mutex>

namespace visual {

enum class AlarmLevel {
  kInfo = 0,
  kWarning = 1,
  kCritical = 2,
};

struct AlarmRecord {
  qint64 timestamp_ms = 0;
  AlarmLevel level = AlarmLevel::kWarning;
  QString subsystem;   // 如 "PLC" / "Camera" / "Algo" / "Engine"
  QString message;
  QString cycle_id;    // 可选，周期关联号
};

class AlarmService : public QObject {
  Q_OBJECT
 public:
  static AlarmService& Instance();

  /** 产生一条报警：写历史、落盘、发信号。 */
  void Raise(AlarmLevel level, const QString& subsystem, const QString& message,
             const QString& cycle_id = QString());

  QVector<AlarmRecord> Recent(int max_count = 100) const;

  void ClearHistory();

 signals:
  void AlarmRaised(const visual::AlarmRecord& record);

 private:
  AlarmService() = default;
  void AppendHistory(const AlarmRecord& record);
  void Persist(const AlarmRecord& record);

  mutable std::mutex mutex_;
  QVector<AlarmRecord> history_;
  static constexpr int kMaxHistory = 500;
};

}  // namespace visual

Q_DECLARE_METATYPE(visual::AlarmRecord)

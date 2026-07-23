/**
 * @file alarm_service.cpp
 * @brief 报警服务；跨线程 Raise 排队到本对象线程，避免 QFile/信号直连 UI 线程不安全。
 */
#include "visual/alarm_service.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTextStream>
#include <QThread>

#include "visual/rotating_file_log.h"

namespace visual {

AlarmService& AlarmService::Instance() {
  static AlarmService svc;
  return svc;
}

void AlarmService::Raise(AlarmLevel level, const QString& subsystem, const QString& message,
                         const QString& cycle_id) {
  if (QThread::currentThread() != thread()) {
    const AlarmLevel lv = level;
    const QString sub = subsystem;
    const QString msg = message;
    const QString cid = cycle_id;
    QMetaObject::invokeMethod(
        this, [this, lv, sub, msg, cid]() { Raise(lv, sub, msg, cid); }, Qt::QueuedConnection);
    return;
  }

  AlarmRecord rec;
  rec.timestamp_ms = QDateTime::currentMSecsSinceEpoch();
  rec.level = level;
  rec.subsystem = subsystem;
  rec.message = message;
  rec.cycle_id = cycle_id;
  AppendHistory(rec);
  Persist(rec);
  emit AlarmRaised(rec);
}

QVector<AlarmRecord> AlarmService::Recent(int max_count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (max_count <= 0 || history_.size() <= max_count) {
    return history_;
  }
  return history_.mid(history_.size() - max_count);
}

void AlarmService::ClearHistory() {
  std::lock_guard<std::mutex> lock(mutex_);
  history_.clear();
}

void AlarmService::AppendHistory(const AlarmRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  history_.push_back(record);
  while (history_.size() > kMaxHistory) {
    history_.removeFirst();
  }
}

void AlarmService::Persist(const AlarmRecord& record) {
  QDir().mkpath(QStringLiteral("./logs"));
  const QString path = QStringLiteral("./logs/alarms.jsonl");
  RotatingFileLog::RotateBySize(path.toStdString(), 8ull * 1024 * 1024, 9);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }
  const char* level = "warning";
  if (record.level == AlarmLevel::kInfo) {
    level = "info";
  } else if (record.level == AlarmLevel::kCritical) {
    level = "critical";
  }
  QJsonObject obj;
  obj.insert(QStringLiteral("ts"), static_cast<qint64>(record.timestamp_ms));
  obj.insert(QStringLiteral("level"), QString::fromUtf8(level));
  obj.insert(QStringLiteral("subsystem"), record.subsystem);
  obj.insert(QStringLiteral("message"), record.message);
  obj.insert(QStringLiteral("cycleId"), record.cycle_id);
  // Qt5 QTextStream 默认本地编码；显式 UTF-8，与 visual_system.log 一致
  QTextStream out(&file);
  out.setCodec("UTF-8");
  out << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << "\n";
}

}  // namespace visual

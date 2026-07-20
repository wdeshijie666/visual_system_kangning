#pragma once

#include <QTableWidget>

#include "visual/station_types.h"

class StationResultWidget : public QTableWidget {
  Q_OBJECT
 public:
  explicit StationResultWidget(const QString& title, QWidget* parent = nullptr);
  void UpdateResults(const visual::LogResultBatch& logs);
};

#include "station_result_widget.h"

#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QPalette>
#include <QTableWidget>

StationResultWidget::StationResultWidget(const QString& title, QWidget* parent) : QTableWidget(parent) {
  setColumnCount(6);
  setRowCount(static_cast<int>(visual::kLogCountPerStation));
  setHorizontalHeaderLabels({"Status", "X(mm)", "Y(mm)", "R(deg)", "Diameter", "Length"});
  setVerticalHeaderLabels({"Log1", "Log2", "Log3", "Log4", "Log5"});
  horizontalHeader()->setStretchLastSection(true);
  horizontalHeader()->setHighlightSections(false);
  verticalHeader()->setHighlightSections(false);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setFrameShape(QFrame::NoFrame);
  setObjectName(title);

  QPalette pal = palette();
  pal.setColor(QPalette::Base, QColor("#252526"));
  pal.setColor(QPalette::AlternateBase, QColor("#252526"));
  pal.setColor(QPalette::Window, QColor("#252526"));
  setPalette(pal);
}

void StationResultWidget::UpdateResults(const visual::LogResultBatch& logs) {
  for (int i = 0; i < static_cast<int>(logs.size()); ++i) {
    const auto& log = logs[static_cast<std::size_t>(i)];
    QString status_text;
    if (log.status == visual::InspectStatus::kOk) {
      status_text = QStringLiteral("OK");
    } else if (log.status == visual::InspectStatus::kNg) {
      status_text = QStringLiteral("NG");
    } else {
      status_text = QString::number(static_cast<int>(log.status));
    }
    auto* status_item = new QTableWidgetItem(status_text);
    if (log.status == visual::InspectStatus::kNg) {
      status_item->setForeground(QBrush(QColor("#e74c3c")));
    } else if (log.status == visual::InspectStatus::kOk) {
      status_item->setForeground(QBrush(QColor("#2ecc71")));
    }
    setItem(i, 0, status_item);
    setItem(i, 1, new QTableWidgetItem(QString::number(log.offset_x_mm, 'f', 2)));
    setItem(i, 2, new QTableWidgetItem(QString::number(log.offset_y_mm, 'f', 2)));
    setItem(i, 3, new QTableWidgetItem(QString::number(log.offset_r_deg, 'f', 2)));
    setItem(i, 4, new QTableWidgetItem(QString::number(log.diameter_mm, 'f', 1)));
    setItem(i, 5, new QTableWidgetItem(QString::number(log.length_mm, 'f', 1)));
  }
}

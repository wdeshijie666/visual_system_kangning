#include "offline_test_widget.h"

#include <QHBoxLayout>
#include <QPushButton>

#include "visual/app_context.h"

OfflineTestWidget::OfflineTestWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  r05_button_ = new QPushButton(tr("手动触发 R05 工位"), this);
  r09_button_ = new QPushButton(tr("手动触发 R09 工位"), this);
  connect(r05_button_, &QPushButton::clicked, this, &OfflineTestWidget::OnRunR05);
  connect(r09_button_, &QPushButton::clicked, this, &OfflineTestWidget::OnRunR09);
  layout->addWidget(r05_button_);
  layout->addWidget(r09_button_);
  RefreshStationEnableFromSettings();
}

void OfflineTestWidget::SetRunHandler(std::function<void(int station)> handler) {
  run_handler_ = std::move(handler);
}

void OfflineTestWidget::SetOfflineTestEnabled(bool enabled) {
  engine_allows_offline_ = enabled;
  ApplyButtonEnableState();
}

void OfflineTestWidget::SetBusy(bool busy) {
  busy_ = busy;
  ApplyButtonEnableState();
}

void OfflineTestWidget::RefreshStationEnableFromSettings() {
  ApplyButtonEnableState();
}

void OfflineTestWidget::ApplyButtonEnableState() {
  const auto& settings = visual::AppContext::Instance().Settings();
  // 三条件同时满足才可点：产线已停、无 Busy、工位启用
  const bool base = engine_allows_offline_ && !busy_;

  if (r05_button_ != nullptr) {
    const bool on = base && settings.station_r05.enabled;
    r05_button_->setEnabled(on);
    if (busy_) {
      r05_button_->setToolTip(tr("手动/回放进行中，请等待结束"));
    } else if (!engine_allows_offline_) {
      r05_button_->setToolTip(tr("产线运行中，请先停止后再进行离线测试"));
    } else if (!settings.station_r05.enabled) {
      r05_button_->setToolTip(tr("R05 工位已禁用 (stations.r05.enabled=false)"));
    } else {
      r05_button_->setToolTip(QString());
    }
  }
  if (r09_button_ != nullptr) {
    const bool on = base && settings.station_r09.enabled;
    r09_button_->setEnabled(on);
    if (busy_) {
      r09_button_->setToolTip(tr("手动/回放进行中，请等待结束"));
    } else if (!engine_allows_offline_) {
      r09_button_->setToolTip(tr("产线运行中，请先停止后再进行离线测试"));
    } else if (!settings.station_r09.enabled) {
      r09_button_->setToolTip(tr("R09 工位已禁用 (stations.r09.enabled=false)"));
    } else {
      r09_button_->setToolTip(QString());
    }
  }

  if (busy_) {
    setToolTip(tr("手动/回放进行中，请等待结束"));
  } else if (!engine_allows_offline_) {
    setToolTip(tr("产线运行中，请先停止后再进行离线测试"));
  } else {
    setToolTip(QString());
  }
}

void OfflineTestWidget::OnRunR05() {
  // 按钮已灰显时一般进不来；仍做 Busy/handler 空保护，防快捷键或残留信号
  if (busy_ || !run_handler_) {
    return;
  }
  run_handler_(5);
}

void OfflineTestWidget::OnRunR09() {
  if (busy_ || !run_handler_) {
    return;
  }
  run_handler_(9);
}

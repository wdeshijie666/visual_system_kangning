#include "offline_test_widget.h"

#include <QHBoxLayout>
#include <QPushButton>

OfflineTestWidget::OfflineTestWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  r05_button_ = new QPushButton(tr("手动触发 R05 工位"), this);
  r09_button_ = new QPushButton(tr("手动触发 R09 工位"), this);
  connect(r05_button_, &QPushButton::clicked, this, &OfflineTestWidget::OnRunR05);
  connect(r09_button_, &QPushButton::clicked, this, &OfflineTestWidget::OnRunR09);
  layout->addWidget(r05_button_);
  layout->addWidget(r09_button_);
}

void OfflineTestWidget::SetRunHandler(std::function<void(int station)> handler) {
  run_handler_ = std::move(handler);
}

void OfflineTestWidget::SetOfflineTestEnabled(bool enabled) {
  if (r05_button_ != nullptr) {
    r05_button_->setEnabled(enabled);
  }
  if (r09_button_ != nullptr) {
    r09_button_->setEnabled(enabled);
  }
  setToolTip(enabled ? QString() : tr("产线运行中，请先停止后再进行离线测试"));
}

void OfflineTestWidget::OnRunR05() {
  if (run_handler_) {
    run_handler_(5);
  }
}

void OfflineTestWidget::OnRunR09() {
  if (run_handler_) {
    run_handler_(9);
  }
}

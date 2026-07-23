/**
 * @file startup_splash.cpp
 * @brief 启动等待动画：深色底、产品名、状态文案、不确定进度条与省略号动画。
 */
#include "startup_splash.h"

#include <QApplication>
#include <QEventLoop>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

StartupSplash::StartupSplash(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("startupSplash"));
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setFixedSize(420, 220);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(28, 28, 28, 24);
  root->setSpacing(12);

  brand_ = new QLabel(QStringLiteral("SmartGuide"), this);
  brand_->setAlignment(Qt::AlignCenter);
  brand_->setStyleSheet(QStringLiteral(
      "QLabel { color: #ffffff; font-size: 28px; font-weight: 600;"
      " font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; }"));

  auto* company = new QLabel(QStringLiteral("TanlyMind"), this);
  company->setAlignment(Qt::AlignCenter);
  company->setStyleSheet(QStringLiteral(
      "QLabel { color: #8a8a8a; font-size: 13px;"
      " font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; }"));

  auto* line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setFixedHeight(1);
  line->setStyleSheet(QStringLiteral("background-color: #3f3f46; border: none;"));

  status_ = new QLabel(QStringLiteral("正在初始化"), this);
  status_->setAlignment(Qt::AlignCenter);
  status_->setWordWrap(true);
  status_->setStyleSheet(QStringLiteral(
      "QLabel { color: #d4d4d4; font-size: 13px;"
      " font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; }"));

  bar_ = new QProgressBar(this);
  bar_->setRange(0, 0);  // 不确定进度（忙碌动画）
  bar_->setTextVisible(false);
  bar_->setFixedHeight(6);
  bar_->setStyleSheet(QStringLiteral(
      "QProgressBar {"
      "  background-color: #252526;"
      "  border: 1px solid #3f3f46;"
      "  border-radius: 3px;"
      "}"
      "QProgressBar::chunk {"
      "  background-color: #094771;"
      "  border-radius: 2px;"
      "}"));

  root->addStretch(1);
  root->addWidget(brand_);
  root->addWidget(company);
  root->addSpacing(4);
  root->addWidget(line);
  root->addSpacing(8);
  root->addWidget(status_);
  root->addWidget(bar_);
  root->addStretch(1);

  setStyleSheet(QStringLiteral(
      "QWidget#startupSplash {"
      "  background-color: #1e1e1e;"
      "  border: 1px solid #3f3f46;"
      "  border-radius: 6px;"
      "}"));

  if (QScreen* screen = QApplication::primaryScreen()) {
    const QRect geo = screen->availableGeometry();
    move(geo.center() - rect().center());
  }

  status_base_ = QStringLiteral("正在初始化");
  dots_timer_ = new QTimer(this);
  dots_timer_->setInterval(420);
  QObject::connect(dots_timer_, &QTimer::timeout, this, [this]() {
    dots_ = (dots_ + 1) % 4;
    QString dots;
    for (int i = 0; i < dots_; ++i) {
      dots += QLatin1Char('.');
    }
    status_->setText(status_base_ + dots);
  });
  dots_timer_->start();
}

void StartupSplash::SetStatus(const QString& text) {
  status_base_ = text.isEmpty() ? QStringLiteral("正在初始化") : text;
  dots_ = 0;
  status_->setText(status_base_);
  Pump();
}

void StartupSplash::Pump() {
  QApplication::processEvents(QEventLoop::AllEvents, 50);
}

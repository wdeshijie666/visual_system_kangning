#include "viewport_widget.h"

#include <QEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QWheelEvent>

namespace {

constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 40.0;
constexpr qreal kZoomStep = 1.15;

}  // namespace

ViewportWidget::ViewportWidget(const QString& title, QWidget* parent) : QGroupBox(title, parent) {
  setFlat(true);
  auto* root = new QStackedLayout(this);
  root->setContentsMargins(8, 10, 8, 8);

  empty_label_ = new QLabel(tr("暂无灰度图"), this);
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setMinimumSize(120, 90);

  scene_ = new QGraphicsScene(this);
  view_ = new QGraphicsView(scene_, this);
  view_->setMinimumSize(120, 90);
  view_->setFrameShape(QFrame::NoFrame);
  view_->setBackgroundBrush(QColor(32, 32, 32));
  view_->setRenderHint(QPainter::SmoothPixmapTransform, true);
  view_->setDragMode(QGraphicsView::ScrollHandDrag);
  view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
  view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
  view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  view_->viewport()->installEventFilter(this);

  pixmap_item_ = scene_->addPixmap(QPixmap());
  pixmap_item_->setTransformationMode(Qt::SmoothTransformation);

  root->addWidget(empty_label_);
  root->addWidget(view_);
  root->setCurrentWidget(empty_label_);
}

void ViewportWidget::SetPreviewImage(const QByteArray& bytes, int width, int height,
                                     visual::ImagePixelFormat format) {
  if (bytes.isEmpty() || width <= 0 || height <= 0) {
    ClearGrayImage();
    return;
  }
  const int bpp = static_cast<int>(visual::BytesPerImagePixel(format));
  if (bpp <= 0 || bytes.size() < width * height * bpp) {
    ClearGrayImage();
    return;
  }

  QImage view;
  switch (format) {
    case visual::ImagePixelFormat::kMono8:
      view = QImage(reinterpret_cast<const uchar*>(bytes.constData()), width, height, width,
                    QImage::Format_Grayscale8)
                 .copy();
      break;
    case visual::ImagePixelFormat::kBgr8: {
      // Qt5 广泛支持 RGB888：BGR → RGB 拷贝
      view = QImage(width, height, QImage::Format_RGB888);
      const auto* src = reinterpret_cast<const uchar*>(bytes.constData());
      for (int y = 0; y < height; ++y) {
        const uchar* s = src + y * width * 3;
        uchar* d = view.scanLine(y);
        for (int x = 0; x < width; ++x) {
          d[x * 3 + 0] = s[x * 3 + 2];
          d[x * 3 + 1] = s[x * 3 + 1];
          d[x * 3 + 2] = s[x * 3 + 0];
        }
      }
      break;
    }
    case visual::ImagePixelFormat::kRgb8:
      view = QImage(reinterpret_cast<const uchar*>(bytes.constData()), width, height, width * 3,
                    QImage::Format_RGB888)
                 .copy();
      break;
    case visual::ImagePixelFormat::kBgra8:
    case visual::ImagePixelFormat::kRgba8: {
      view = QImage(width, height, QImage::Format_RGB888);
      const auto* src = reinterpret_cast<const uchar*>(bytes.constData());
      const bool bgra = (format == visual::ImagePixelFormat::kBgra8);
      for (int y = 0; y < height; ++y) {
        const uchar* s = src + y * width * 4;
        uchar* d = view.scanLine(y);
        for (int x = 0; x < width; ++x) {
          if (bgra) {
            d[x * 3 + 0] = s[x * 4 + 2];
            d[x * 3 + 1] = s[x * 4 + 1];
            d[x * 3 + 2] = s[x * 4 + 0];
          } else {
            d[x * 3 + 0] = s[x * 4 + 0];
            d[x * 3 + 1] = s[x * 4 + 1];
            d[x * 3 + 2] = s[x * 4 + 2];
          }
        }
      }
      break;
    }
    default:
      ClearGrayImage();
      return;
  }

  source_image_ = view;
  pixmap_item_->setPixmap(QPixmap::fromImage(source_image_));
  scene_->setSceneRect(pixmap_item_->boundingRect());
  has_user_transform_ = false;
  auto* root = qobject_cast<QStackedLayout*>(layout());
  if (root != nullptr) {
    root->setCurrentWidget(view_);
  }
  FitInViewIfNeeded(true);
}

void ViewportWidget::SetGrayImage(const QByteArray& mono8, int width, int height) {
  SetPreviewImage(mono8, width, height, visual::ImagePixelFormat::kMono8);
}

void ViewportWidget::ClearGrayImage() {
  source_image_ = QImage();
  pixmap_item_->setPixmap(QPixmap());
  scene_->setSceneRect(QRectF());
  has_user_transform_ = false;
  view_->resetTransform();
  auto* root = qobject_cast<QStackedLayout*>(layout());
  if (root != nullptr) {
    root->setCurrentWidget(empty_label_);
  }
}

void ViewportWidget::ResetView() {
  if (source_image_.isNull()) {
    return;
  }
  has_user_transform_ = false;
  FitInViewIfNeeded(true);
}

void ViewportWidget::resizeEvent(QResizeEvent* event) {
  QGroupBox::resizeEvent(event);
  if (!source_image_.isNull() && !has_user_transform_) {
    FitInViewIfNeeded(true);
  }
}

bool ViewportWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_->viewport() && !source_image_.isNull()) {
    if (event->type() == QEvent::Wheel) {
      auto* wheel = static_cast<QWheelEvent*>(event);
      if (wheel->angleDelta().y() != 0) {
        const qreal factor = wheel->angleDelta().y() > 0 ? kZoomStep : (1.0 / kZoomStep);
        ApplyZoom(factor, wheel->position());
        has_user_transform_ = true;
        return true;
      }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
      ResetView();
      return true;
    } else if (event->type() == QEvent::MouseButtonPress) {
      // 拖拽平移后不再自动 fit
      has_user_transform_ = true;
    }
  }
  return QGroupBox::eventFilter(watched, event);
}

void ViewportWidget::FitInViewIfNeeded(bool force) {
  if (source_image_.isNull() || view_ == nullptr || pixmap_item_ == nullptr) {
    return;
  }
  if (!force && has_user_transform_) {
    return;
  }
  view_->resetTransform();
  view_->fitInView(pixmap_item_, Qt::KeepAspectRatio);
}

void ViewportWidget::ApplyZoom(double factor, const QPointF& /*anchor_view_pos*/) {
  if (view_ == nullptr || qFuzzyCompare(factor, 1.0)) {
    return;
  }
  const qreal current = view_->transform().m11();
  const qreal target = qBound(current * factor, kMinScale, kMaxScale);
  if (qFuzzyCompare(current, target)) {
    return;
  }
  view_->scale(target / current, target / current);
}

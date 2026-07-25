#pragma once

#include <QByteArray>
#include <QGroupBox>
#include <QImage>
#include <QString>

#include "visual/capture_data_format.h"

class QGraphicsPixmapItem;
class QGraphicsScene;
class QGraphicsView;
class QLabel;

/** 工位灰度图视口：Mono8 预览，支持滚轮缩放与拖拽平移。 */
class ViewportWidget : public QGroupBox {
  Q_OBJECT
 public:
  explicit ViewportWidget(const QString& title, QWidget* parent = nullptr);

  /** 预览图：支持 Mono8 / BGR / RGB（自适应）。 */
  void SetPreviewImage(const QByteArray& bytes, int width, int height,
                       visual::ImagePixelFormat format);
  void SetGrayImage(const QByteArray& mono8, int width, int height);
  void ClearGrayImage();

  /** 重置为适应窗口（完整可见）。 */
  void ResetView();

 protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void FitInViewIfNeeded(bool force);
  void ApplyZoom(double factor, const QPointF& anchor_view_pos);

  QGraphicsView* view_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  QGraphicsPixmapItem* pixmap_item_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QImage source_image_;
  bool has_user_transform_ = false;
};

/**
 * @file camera_manager_widget.cpp
 * @brief 配方参数树 + 按序列号选择相机后导入。
 */
#include "camera_manager_widget.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QFileDialog>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

CameraManagerWidget::CameraManagerWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);

  auto* cam_row = new QHBoxLayout();
  cam_row->addWidget(new QLabel(tr("目标相机(序列号):"), this));
  camera_combo_ = new QComboBox(this);
  camera_combo_->setMinimumWidth(220);
  cam_row->addWidget(camera_combo_, 1);
  layout->addLayout(cam_row);

  tree_ = new QTreeWidget(this);
  tree_->setHeaderLabels({tr("参数"), tr("值")});
  auto* hdr = new QTreeWidgetItem(tree_, {tr("拍摄参数"), tr("—")});
  new QTreeWidgetItem(hdr, {tr("曝光"), tr("默认")});
  new QTreeWidgetItem(tree_, {tr("HDR参数"), tr("—")});
  new QTreeWidgetItem(tree_, {tr("后处理参数"), tr("—")});
  tree_->expandAll();
  layout->addWidget(tree_, 1);

  auto* btn = new QPushButton(tr("导入配方文件"), this);
  connect(btn, &QPushButton::clicked, this, &CameraManagerWidget::OnImportRecipe);
  layout->addWidget(btn);
}

void CameraManagerWidget::SetImportRecipeHandler(
    std::function<void(const QString& camera_id, const QString& recipe_path)> handler) {
  import_handler_ = std::move(handler);
}

void CameraManagerWidget::SetCameraChoices(const std::vector<CameraChoice>& cameras) {
  if (camera_combo_ == nullptr) {
    return;
  }
  camera_combo_->clear();
  for (const auto& c : cameras) {
    QString label = c.serial.isEmpty() ? c.camera_id : c.serial;
    if (!c.connected) {
      label += tr(" (未连接)");
    }
    camera_combo_->addItem(label, c.camera_id);
  }
  if (camera_combo_->count() > 0) {
    camera_combo_->setCurrentIndex(0);
  }
}

QString CameraManagerWidget::SelectedCameraId() const {
  if (camera_combo_ == nullptr || camera_combo_->count() == 0) {
    return {};
  }
  return camera_combo_->currentData().toString();
}

void CameraManagerWidget::SetRecipeParams(const visual::RecipeParamList& params) {
  if (tree_ == nullptr) {
    return;
  }
  tree_->clear();
  if (params.empty()) {
    new QTreeWidgetItem(tree_, {tr("拍摄参数"), tr("—")});
    new QTreeWidgetItem(tree_, {tr("HDR参数"), tr("—")});
    new QTreeWidgetItem(tree_, {tr("后处理参数"), tr("—")});
    return;
  }

  QHash<QString, QTreeWidgetItem*> groups;
  for (const auto& item : params) {
    const QString group = QString::fromStdString(item.group_zh);
    QTreeWidgetItem* parent = groups.value(group, nullptr);
    if (parent == nullptr) {
      parent = new QTreeWidgetItem(tree_, {group, QString()});
      groups.insert(group, parent);
    }
    new QTreeWidgetItem(parent, {QString::fromStdString(item.name_zh),
                                 QString::fromStdString(item.value)});
  }
  tree_->expandAll();
}

void CameraManagerWidget::OnImportRecipe() {
  const QString camera_id = SelectedCameraId();
  if (camera_id.isEmpty()) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, tr("选择 RVC 配方文件"), QString(),
      tr("RVC 配方 (*.json);;所有文件 (*.*)"));
  if (path.isEmpty()) {
    return;
  }
  if (import_handler_) {
    import_handler_(camera_id, path);
  }
}

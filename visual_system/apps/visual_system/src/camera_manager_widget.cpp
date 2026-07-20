#include "camera_manager_widget.h"

#include <QFileDialog>
#include <QHash>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

CameraManagerWidget::CameraManagerWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
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

void CameraManagerWidget::SetImportRecipeHandler(std::function<void(const QString&)> handler) {
  import_handler_ = std::move(handler);
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
  const QString path = QFileDialog::getOpenFileName(
      this, tr("选择 RVC 配方文件"), QString(),
      tr("RVC 配方 (*.json);;所有文件 (*.*)"));
  if (path.isEmpty()) {
    return;
  }
  if (import_handler_) {
    import_handler_(path);
  }
}

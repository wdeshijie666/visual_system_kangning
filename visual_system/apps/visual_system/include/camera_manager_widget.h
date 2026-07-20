#pragma once

#include <functional>
#include <QWidget>

#include "visual/camera_recipe.h"

class QTreeWidget;

class CameraManagerWidget : public QWidget {
  Q_OBJECT
 public:
  explicit CameraManagerWidget(QWidget* parent = nullptr);
  void SetImportRecipeHandler(std::function<void(const QString&)> handler);

  /** 用导入后的配方参数刷新上方参数表（中文名）。 */
  void SetRecipeParams(const visual::RecipeParamList& params);

 private slots:
  void OnImportRecipe();

 private:
  std::function<void(const QString&)> import_handler_;
  QTreeWidget* tree_ = nullptr;
};

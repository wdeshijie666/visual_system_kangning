/**
 * @file camera_manager_widget.h
 * @brief 相机配方参数表与按序列号导入入口。
 */
#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <QWidget>

#include "visual/camera_recipe.h"

class QComboBox;
class QTreeWidget;

struct CameraChoice {
  QString camera_id;
  QString serial;
  bool connected = false;
};

class CameraManagerWidget : public QWidget {
  Q_OBJECT
 public:
  explicit CameraManagerWidget(QWidget* parent = nullptr);

  /**
   * 导入回调：仅针对下拉框当前选中的相机。
   * @param camera_id devices.json 中的 id
   * @param recipe_path 用户选择的配方文件
   */
  void SetImportRecipeHandler(std::function<void(const QString& camera_id, const QString& recipe_path)>
                                  handler);

  /** 填充「按序列号」相机列表；展示文案为序列号，可附带离线提示。 */
  void SetCameraChoices(const std::vector<CameraChoice>& cameras);

  /** 当前选中的相机 id；无选中返回空。 */
  QString SelectedCameraId() const;

  /** 用导入后的配方参数刷新上方参数表（中文名）。 */
  void SetRecipeParams(const visual::RecipeParamList& params);

 private slots:
  void OnImportRecipe();

 private:
  std::function<void(const QString& camera_id, const QString& recipe_path)> import_handler_;
  QComboBox* camera_combo_ = nullptr;
  QTreeWidget* tree_ = nullptr;
};

/**
 * @file camera_recipe.h
 * @brief 相机配方参数（导入后供 UI 表格展示）。
 */
#pragma once

#include <string>
#include <vector>

namespace visual {

struct RecipeParamItem {
  std::string group_zh;  /**< 参数组中文名，如「拍摄参数」 */
  std::string name_zh;   /**< 参数中文名 */
  std::string value;     /**< 显示用字符串 */
};

using RecipeParamList = std::vector<RecipeParamItem>;

}  // namespace visual

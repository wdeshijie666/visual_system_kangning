/**
 * @file camera_recipe_paths.h
 * @brief 默认配方路径：程序目录/camera_parmas/{序列号}.json
 */
#pragma once

#include <filesystem>
#include <string>

namespace visual {

/** SmartGuide.exe（应用）所在目录。 */
std::filesystem::path ResolveHostExeDir();

/**
 * 默认配方文件：{exe}/camera_parmas/{serial}.json
 * serial 为空时返回空路径。
 */
std::filesystem::path DefaultRecipePathForSerial(const std::string& serial);

}  // namespace visual

/**
 * @file camera_recipe_paths.cpp
 * @brief 解析 exe 旁 camera_parmas/{sn}.json 默认配方路径。
 */
#include "camera_recipe_paths.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace visual {

std::filesystem::path ResolveHostExeDir() {
#ifdef _WIN32
  char buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(buf).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

std::filesystem::path DefaultRecipePathForSerial(const std::string& serial) {
  if (serial.empty()) {
    return {};
  }
  return ResolveHostExeDir() / "camera_parmas" / (serial + ".json");
}

}  // namespace visual

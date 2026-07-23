/**
 * @file pcp_c_api.cpp
 * @brief 必须与 PointCloudProcessor.dll 使用同一 MSVC 工具集（v143 / 14.44）编译。
 */
#include "pcp_c_api.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "../PointCloudProcessor_api.h"

struct PCP_Handle {
  std::unique_ptr<PointCloudProcessor> proc;
};

namespace {

void SetError(char* err_buf, int err_buf_len, const std::string& msg) {
  if (err_buf == nullptr || err_buf_len <= 0) {
    return;
  }
  if (msg.empty()) {
    err_buf[0] = '\0';
    return;
  }
  const int n = err_buf_len - 1;
  std::strncpy(err_buf, msg.c_str(), static_cast<size_t>(n));
  err_buf[n] = '\0';
}

bool ValidateConfigFile(const std::string& config_path, std::string* error) {
  std::ifstream in(config_path, std::ios::binary);
  if (!in) {
    if (error) {
      *error = "无法打开算法配置: " + config_path;
    }
    return false;
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  const std::string text = oss.str();
  if (text.empty()) {
    if (error) {
      *error = "算法配置为空: " + config_path;
    }
    return false;
  }
  try {
    const auto j = nlohmann::json::parse(text);
    if (!j.is_object()) {
      if (error) {
        *error = "算法配置不是 JSON 对象: " + config_path;
      }
      return false;
    }
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("算法配置 JSON 无效: ") + ex.what();
    }
    return false;
  }
  return true;
}

cv::Mat MakeMat(const void* data, int rows, int cols, int type) {
  if (data == nullptr || rows <= 0 || cols <= 0) {
    return {};
  }
  return cv::Mat(rows, cols, type, const_cast<void*>(data));
}

}  // namespace

PCP_Handle* pcp_create(const char* config_path, char* err_buf, int err_buf_len) {
  if (config_path == nullptr || config_path[0] == '\0') {
    SetError(err_buf, err_buf_len, "算法配置路径为空");
    return nullptr;
  }
  std::string validate_error;
  if (!ValidateConfigFile(config_path, &validate_error)) {
    SetError(err_buf, err_buf_len, validate_error);
    return nullptr;
  }
  try {
    auto handle = std::make_unique<PCP_Handle>();
    handle->proc = std::make_unique<PointCloudProcessor>(std::string(config_path));
    return handle.release();
  } catch (const std::exception& ex) {
    SetError(err_buf, err_buf_len, std::string("算法引擎构造异常: ") + ex.what());
    return nullptr;
  } catch (...) {
    SetError(err_buf, err_buf_len,
             "算法引擎构造失败(原生异常/访问冲突)，请确认 alg_program 依赖 DLL 完整且勿混用管理员/"
             "普通权限");
    return nullptr;
  }
}

void pcp_destroy(PCP_Handle* handle) {
  delete handle;
}

int pcp_load_depth_map(PCP_Handle* handle, const void* data, int rows, int cols, int type) {
  if (handle == nullptr || handle->proc == nullptr) {
    return 0;
  }
  try {
    const cv::Mat depth = MakeMat(data, rows, cols, type);
    if (depth.empty()) {
      return 0;
    }
    return handle->proc->loadDepthMap(depth) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

size_t pcp_get_point_count(PCP_Handle* handle) {
  if (handle == nullptr || handle->proc == nullptr) {
    return 0;
  }
  try {
    return handle->proc->getPointCount();
  } catch (...) {
    return 0;
  }
}

int pcp_process(PCP_Handle* handle, const void* data, int rows, int cols, int type, int top_n) {
  if (handle == nullptr || handle->proc == nullptr) {
    return -999;
  }
  try {
    const cv::Mat depth = MakeMat(data, rows, cols, type);
    if (depth.empty()) {
      return -999;
    }
    return handle->proc->process(depth, top_n);
  } catch (...) {
    return -999;
  }
}

size_t pcp_get_cluster_count(PCP_Handle* handle) {
  if (handle == nullptr || handle->proc == nullptr) {
    return 0;
  }
  try {
    return handle->proc->getClusters().size();
  } catch (...) {
    return 0;
  }
}

int pcp_get_fit_results(PCP_Handle* handle, PCP_C_FitResult* out, int max_count) {
  if (handle == nullptr || handle->proc == nullptr || out == nullptr || max_count <= 0) {
    return 0;
  }
  try {
    const auto& fits = handle->proc->getFitResults();
    const int n = static_cast<int>(fits.size()) < max_count ? static_cast<int>(fits.size())
                                                            : max_count;
    for (int i = 0; i < n; ++i) {
      out[i].log_index = fits[static_cast<size_t>(i)].log_index;
      out[i].valid = fits[static_cast<size_t>(i)].valid ? 1 : 0;
      out[i].offset_x = fits[static_cast<size_t>(i)].offset_x;
      out[i].offset_y = fits[static_cast<size_t>(i)].offset_y;
      out[i].tilt_deg = fits[static_cast<size_t>(i)].tilt_deg;
      out[i].diameter = fits[static_cast<size_t>(i)].diameter;
      out[i].length = fits[static_cast<size_t>(i)].length;
    }
    return n;
  } catch (...) {
    return 0;
  }
}

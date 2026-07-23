/**
 * @file pcp_c_api.h
 * @brief PointCloudProcessor 的纯 C ABI 桥接（跨 MSVC 工具集安全）。
 *
 * PointCloudProcessor.dll 由 VS2022 (14.44) 编译；本 DLL 亦须用同一工具集构建。
 * mock_algo_service（可为 VS2019）仅通过本头文件调用，禁止直接链接 PointCloudProcessor。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PCP_C_API_EXPORTS
#  define PCP_C_API __declspec(dllexport)
#else
#  define PCP_C_API __declspec(dllimport)
#endif

typedef struct PCP_Handle PCP_Handle;

typedef struct PCP_C_FitResult {
  uint16_t log_index;
  int valid;
  double offset_x;
  double offset_y;
  double tilt_deg;
  double diameter;
  double length;
} PCP_C_FitResult;

/** 构造引擎。失败返回 NULL，并将错误写入 err_buf（可为 NULL）。 */
PCP_C_API PCP_Handle* pcp_create(const char* config_path, char* err_buf, int err_buf_len);

PCP_C_API void pcp_destroy(PCP_Handle* handle);

/**
 * 加载深度图。data 指向连续像素缓冲；type 为 OpenCV type（常用 CV_32FC1=5）。
 * 返回 1 成功，0 失败。
 */
PCP_C_API int pcp_load_depth_map(PCP_Handle* handle, const void* data, int rows, int cols,
                                 int type);

PCP_C_API size_t pcp_get_point_count(PCP_Handle* handle);

/** 返回检出数量；失败返回 -999。 */
PCP_C_API int pcp_process(PCP_Handle* handle, const void* data, int rows, int cols, int type,
                          int top_n);

PCP_C_API size_t pcp_get_cluster_count(PCP_Handle* handle);

/** 写入最多 max_count 条结果，返回实际条数。 */
PCP_C_API int pcp_get_fit_results(PCP_Handle* handle, PCP_C_FitResult* out, int max_count);

#ifdef __cplusplus
}
#endif

/**
 * @file algo_run_mode.h
 * @brief 算法进程 algo_config.json 中 mode 字段取值。
 */
#pragma once

namespace visual::algo_config {

/** 0=在线（SHM 与视觉系统交互），1=离线历史数据回放。 */
inline constexpr int kModeOnline = 0;
inline constexpr int kModeOfflineReplay = 1;

}  // namespace visual::algo_config

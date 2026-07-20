#pragma once

#include "algo_result_io.h"

namespace algo {

/** 在线模式：监听 SHM 并与视觉主进程交互。 */
int RunOnlineService(const AlgoConfig& config);

}  // namespace algo

#pragma once

#include "algo_config.h"

namespace algo {

/** 离线回放模式：读取历史目录并输出结果到 offline_result。 */
int RunOfflineReplay(const AlgoConfig& config);

}  // namespace algo

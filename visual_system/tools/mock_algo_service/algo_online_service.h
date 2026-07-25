/**
 * @file algo_online_service.h
 * @brief 算法进程在线服务：按 SHM 通道监听请求。
 */
#pragma once

#include "algo_result_io.h"
#include "visual/algo_shm_layout.h"

namespace algo {

struct PointCloudProcessorSlot;

/** 在线模式：启动 R05/R09 双服务线程。 */
int RunOnlineService(const AlgoConfig& config);

/**
 * 单通道服务循环（R05/R09 各一线程、各绑一块 SHM）。
 * processor_slot：每通道独立引擎（可并行 process，压测时不加全局锁）。
 * @return 异常退出码；正常常驻不返回。
 */
int RunOnlineServiceForChannel(const AlgoConfig& config, visual::shm::ShmChannelId channel,
                               PointCloudProcessorSlot* processor_slot = nullptr);

}  // namespace algo

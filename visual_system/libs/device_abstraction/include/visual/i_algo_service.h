/**
 * @file i_algo_service.h
 * @brief 算法服务抽象（SHM 独立进程或进程内 Mock）。
 */
#pragma once

#include "visual/station_types.h"

namespace visual {

class IAlgoService {
 public:
  virtual ~IAlgoService() = default;

  virtual bool Start() = 0;
  virtual void Stop() = 0;

  virtual bool Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) = 0;
};

}  // namespace visual

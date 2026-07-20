/**
 * @file shm_algo_service.h
 * @brief 通过命名共享内存调用独立算法进程。
 */
#pragma once

#include <memory>
#include <string>

#include "visual/algo_shm_layout.h"
#include "visual/i_algo_service.h"

namespace visual {

class ShmAlgoService final : public IAlgoService {
 public:
  explicit ShmAlgoService(std::string shm_name = shm::kShmName);
  ~ShmAlgoService() override;

  bool Start() override;
  void Stop() override;
  bool Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) override;

 private:
  bool EnsureMapping();
  void CloseMapping();

  std::string shm_name_;
  void* mapping_ = nullptr;
  shm::ShmHeader* header_ = nullptr;
  std::uint8_t* blob_arena_ = nullptr;
  void* mutex_ = nullptr;
  std::uint32_t seq_ = 0;
};

/** 进程内 Mock，不依赖外部算法 EXE。 */
class MockAlgoService final : public IAlgoService {
 public:
  bool Start() override { return true; }
  void Stop() override {}
  bool Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) override;
};

}  // namespace visual

/**
 * @file shm_algo_service.h
 * @brief 通过命名共享内存调用独立算法进程；支持双通道池。
 */
#pragma once

#include <memory>
#include <string>

#include "visual/algo_shm_layout.h"
#include "visual/i_algo_service.h"

namespace visual {

class ShmAlgoService final : public IAlgoService {
 public:
  explicit ShmAlgoService(std::string shm_name = shm::kShmNameR05,
                          std::string mutex_name = shm::kMutexNameR05);
  ~ShmAlgoService() override;

  bool Start() override;
  void Stop() override;
  bool Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) override;

  const std::string& ShmName() const { return shm_name_; }
  const std::string& MutexName() const { return mutex_name_; }

 private:
  bool EnsureMapping();
  void CloseMapping();

  std::string shm_name_;
  std::string mutex_name_;
  void* mapping_ = nullptr;
  shm::ShmHeader* header_ = nullptr;
  std::uint8_t* blob_arena_ = nullptr;
  void* mutex_ = nullptr;
  std::uint32_t seq_ = 0;
};

/**
 * 双工位算法通道池：R05 / R09 各一个 ShmAlgoService。
 * 调用方按 StationId 取通道，禁止两工位共用同一实例。
 */
class ShmAlgoServicePool {
 public:
  void Configure(shm::ShmChannelId channel, std::string shm_name, std::string mutex_name);

  bool Start();
  void Stop();

  IAlgoService& ForStation(StationId station);
  IAlgoService* TryForStation(StationId station);

 private:
  std::shared_ptr<ShmAlgoService> r05_;
  std::shared_ptr<ShmAlgoService> r09_;
};

/** 进程内 Mock，不依赖外部算法 EXE。 */
class MockAlgoService final : public IAlgoService {
 public:
  bool Start() override { return true; }
  void Stop() override {}
  bool Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) override;
};

}  // namespace visual

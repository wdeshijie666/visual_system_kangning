/**
 * @file shm_algo_service.cpp
 * @brief 视觉侧算法通道：通过 SHM v2 与独立算法进程通信。
 *
 * 在线：WriteRequestToShm 写入 blob → state=kRequestPosted → 轮询 kDone
 * 算法侧：algo_online_service.cpp RunOnlineService 循环处理
 * 详见 docs/框架流程通路.md §6.3
 */
#include "visual/algo_shm_codec.h"
#include "visual/simulation_profile.h"
#include "visual/shm_algo_service.h"
#include "visual/station_types.h"

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace visual {
namespace {

LogResult FromShm(const shm::ShmLogResult& s) {
  LogResult r;
  r.status = static_cast<InspectStatus>(s.status);
  r.offset_x_mm = s.offset_x_mm;
  r.offset_y_mm = s.offset_y_mm;
  r.offset_r_deg = s.offset_r_deg;
  r.diameter_mm = s.diameter_mm;
  r.length_mm = s.length_mm;
  return r;
}

}  // namespace

ShmAlgoService::ShmAlgoService(std::string shm_name) : shm_name_(std::move(shm_name)) {}

ShmAlgoService::~ShmAlgoService() {
  Stop();
}

bool ShmAlgoService::EnsureMapping() {
#ifdef _WIN32
  if (header_ != nullptr) {
    return true;
  }
  HANDLE file = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(shm::kShmTotalSize), shm_name_.c_str());
  if (file == nullptr) {
    return false;
  }
  mapping_ = MapViewOfFile(file, FILE_MAP_ALL_ACCESS, 0, 0, shm::kShmTotalSize);
  CloseHandle(file);
  if (mapping_ == nullptr) {
    return false;
  }
  header_ = static_cast<shm::ShmHeader*>(mapping_);
  blob_arena_ = shm::BlobArenaBase(header_);
  if (header_->magic != shm::kMagic || header_->version != shm::kVersion) {
    std::memset(header_, 0, shm::kShmTotalSize);
    header_->magic = shm::kMagic;
    header_->version = shm::kVersion;
    header_->state = shm::State::kIdle;
  }
  mutex_ = CreateMutexA(nullptr, FALSE, shm::kMutexName);
  return mutex_ != nullptr;
#else
  return false;
#endif
}

void ShmAlgoService::CloseMapping() {
#ifdef _WIN32
  if (mapping_ != nullptr) {
    UnmapViewOfFile(mapping_);
    mapping_ = nullptr;
    header_ = nullptr;
    blob_arena_ = nullptr;
  }
  if (mutex_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(mutex_));
    mutex_ = nullptr;
  }
#endif
}

bool ShmAlgoService::Start() {
  // 阶段 1.3：创建/映射 SHM 与互斥量（SequenceEngine::Start 时调用）
  return EnsureMapping();
}

void ShmAlgoService::Stop() {
  CloseMapping();
}

bool ShmAlgoService::Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) {
  if (resp == nullptr || !EnsureMapping()) {
    return false;
  }
#ifdef _WIN32
  HANDLE mtx = static_cast<HANDLE>(mutex_);
  if (WaitForSingleObject(mtx, 5000) != WAIT_OBJECT_0) {
    resp->ok = false;
    resp->message = "shm mutex timeout";
    return false;
  }

  ++seq_;
  header_->seq_id = seq_;
  header_->station_id = static_cast<std::int32_t>(req.station);
  header_->error_message[0] = '\0';

  // 按 input_mode 写入：在线拷贝 blob / 回放仅写 session_dir
  std::string write_error;
  if (!shm::WriteRequestToShm(header_, blob_arena_, shm::kBlobArenaSize, req, &write_error)) {
    resp->ok = false;
    resp->message = write_error.empty() ? "shm write failed" : write_error;
    ReleaseMutex(mtx);
    return false;
  }

  // 通知算法进程：kIdle → kRequestPosted
  header_->state = shm::State::kRequestPosted;
  ReleaseMutex(mtx);

  // 轮询等待算法置 kDone 或 kError（超时由 setting.json algo.timeoutMs 控制）
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (WaitForSingleObject(mtx, 100) != WAIT_OBJECT_0) {
      continue;
    }
    const auto st = header_->state;
    if (st == shm::State::kDone) {
      // 读取算法写回的 5 条 Log，复位为 kIdle
      for (std::size_t i = 0; i < shm::kLogCount; ++i) {
        resp->logs[i] = FromShm(header_->logs[i]);
      }
      resp->ok = true;
      header_->state = shm::State::kIdle;
      ReleaseMutex(mtx);
      return true;
    }
    if (st == shm::State::kError) {
      resp->ok = false;
      resp->message = header_->error_message;
      header_->state = shm::State::kIdle;
      ReleaseMutex(mtx);
      return false;
    }
    ReleaseMutex(mtx);
  }
  resp->ok = false;
  resp->message = "algo timeout";
  return false;
#else
  (void)req;
  (void)timeout_ms;
  resp->ok = false;
  resp->message = "SHM unsupported on this platform";
  return false;
#endif
}

bool MockAlgoService::Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) {
  // useShm=false 时进程内 Mock，不经 SHM，用于无算法 EXE 的调试
  (void)req;
  (void)timeout_ms;
  if (resp == nullptr) {
    return false;
  }

  const auto fill_from_profile = [&](const SimulationResultProfile& profile) {
    for (std::size_t i = 0; i < kLogCountPerStation; ++i) {
      resp->logs[i].status = static_cast<InspectStatus>(profile.status);
      resp->logs[i].offset_x_mm = profile.offset_x_mm;
      resp->logs[i].offset_y_mm = profile.offset_y_mm;
      resp->logs[i].offset_r_deg = profile.offset_r_deg;
      resp->logs[i].diameter_mm = profile.diameter_mm;
      resp->logs[i].length_mm = profile.length_mm;
    }
  };

  if (IsSimulationProfileEnabled()) {
    fill_from_profile(GetSimulationResultProfile());
  } else {
    for (std::size_t i = 0; i < kLogCountPerStation; ++i) {
      resp->logs[i].status = InspectStatus::kOk;
      resp->logs[i].offset_x_mm = 0.1 * static_cast<double>(i + 1);
      resp->logs[i].offset_y_mm = 0.0;
      resp->logs[i].offset_r_deg = 0.0;
      resp->logs[i].diameter_mm = 100.0;
      resp->logs[i].length_mm = 900.0;
    }
  }
  resp->ok = true;
  resp->message.clear();
  return true;
}

}  // namespace visual

/**
 * @file shm_algo_service.cpp
 * @brief 视觉侧算法通道：每工位独立 SHM + Mutex + Event，与算法进程双线程配对。
 */
#include "visual/algo_shm_codec.h"
#include "visual/algo_liveness.h"
#include "visual/capture_data_format.h"
#include "visual/log_format.h"
#include "visual/simulation_profile.h"
#include "visual/shm_algo_service.h"
#include "visual/station_types.h"

#include <chrono>
#include <cstring>
#include <sstream>
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

#ifdef _WIN32
bool WaitShmMutex(HANDLE mtx, DWORD timeout_ms) {
  const DWORD w = WaitForSingleObject(mtx, timeout_ms);
  return w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
}

void InitShmHeaderOnly(shm::ShmHeader* header, std::size_t blob_arena_size) {
  std::memset(header, 0, sizeof(shm::ShmHeader));
  header->magic = shm::kMagic;
  header->version = shm::kVersion;
  header->state = shm::State::kIdle;
  header->blob_arena_bytes = static_cast<std::uint64_t>(blob_arena_size);
  header->vision_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
}
#endif

}  // namespace

ShmAlgoService::ShmAlgoService(std::string shm_name, std::string mutex_name)
    : shm_name_(std::move(shm_name)), mutex_name_(std::move(mutex_name)) {
  if (shm_name_.empty()) {
    shm_name_ = shm::kShmNameR05;
  }
  if (mutex_name_.empty()) {
    mutex_name_ = shm::kMutexNameR05;
  }
  const bool is_r09 = (shm_name_.find("R09") != std::string::npos);
  event_name_ = shm::EventNameForChannel(is_r09 ? shm::ShmChannelId::kR09 : shm::ShmChannelId::kR05);
}

ShmAlgoService::~ShmAlgoService() {
  Stop();
}

void ShmAlgoService::SetTransferFlags(std::uint32_t transfer_flags) {
  transfer_flags_ = transfer_flags;
}

bool ShmAlgoService::EnsureMapping() {
#ifdef _WIN32
  if (header_ != nullptr) {
    return true;
  }
  if (shm_name_.empty()) {
    LogToStderr(LogSeverity::kWarning, "[shm] EnsureMapping: shm_name 为空");
    return false;
  }

  const std::size_t want_total = shm::ShmTotalSizeForFlags(transfer_flags_);
  const std::size_t want_arena = shm::BlobArenaSizeForFlags(transfer_flags_);
  mapped_total_size_ = want_total;
  blob_arena_size_ = want_arena;

  SetLastError(0);
  HANDLE file = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(mapped_total_size_), shm_name_.c_str());
  if (file == nullptr) {
    std::ostringstream oss;
    oss << "[shm] CreateFileMapping 失败 name=" << shm_name_ << " err=" << GetLastError();
    LogToStderr(LogSeverity::kWarning, oss.str());
    return false;
  }
  mapping_handle_ = file;

  mapping_ = MapViewOfFile(file, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (mapping_ == nullptr) {
    std::ostringstream oss;
    oss << "[shm] MapViewOfFile 失败 name=" << shm_name_ << " err=" << GetLastError();
    LogToStderr(LogSeverity::kWarning, oss.str());
    CloseHandle(file);
    mapping_handle_ = nullptr;
    return false;
  }

  // 若打开的是已有映射且实际更小，按可用区域收紧 arena，避免越界写
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(mapping_, &mbi, sizeof(mbi)) != 0 && mbi.RegionSize >= shm::kShmHeaderSize) {
    const std::size_t available = mbi.RegionSize - shm::kShmHeaderSize;
    if (available < blob_arena_size_) {
      blob_arena_size_ = available;
      mapped_total_size_ = shm::kShmHeaderSize + blob_arena_size_;
    }
  }

  header_ = static_cast<shm::ShmHeader*>(mapping_);
  blob_arena_ = shm::BlobArenaBase(header_);
  if (header_->magic != shm::kMagic || header_->version != shm::kVersion ||
      header_->blob_arena_bytes == 0) {
    InitShmHeaderOnly(header_, blob_arena_size_);
  } else {
    header_->blob_arena_bytes = static_cast<std::uint64_t>(blob_arena_size_);
    header_->vision_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
  }

  mutex_ = CreateMutexA(nullptr, FALSE, mutex_name_.c_str());
  event_ = CreateEventA(nullptr, FALSE, FALSE, event_name_.c_str());  // auto-reset
  if (mutex_ == nullptr || event_ == nullptr) {
    LogToStderr(LogSeverity::kWarning, "[shm] CreateMutex/Event 失败");
    CloseMapping();
    return false;
  }

  return true;
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
  if (mapping_handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(mapping_handle_));
    mapping_handle_ = nullptr;
  }
  if (mutex_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(mutex_));
    mutex_ = nullptr;
  }
  if (event_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(event_));
    event_ = nullptr;
  }
#endif
}

bool ShmAlgoService::Start() {
  return EnsureMapping();
}

void ShmAlgoService::Stop() {
  CloseMapping();
}

bool ShmAlgoService::Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) {
  if (resp == nullptr || !EnsureMapping()) {
    if (resp) {
      resp->ok = false;
      resp->message = "shm mapping failed";
    }
    return false;
  }
#ifdef _WIN32
  HANDLE mtx = static_cast<HANDLE>(mutex_);
  HANDLE evt = static_cast<HANDLE>(event_);
  if (!WaitShmMutex(mtx, 5000)) {
    resp->ok = false;
    resp->message = "shm mutex timeout";
    return false;
  }

  // 新请求入队前：回收孤儿状态（超时未复位 / 重启后残留 Done/Posted 等）。
  // 仅在投递前执行，不影响「已 Posted 后的等待」；也不削弱算法附着时保留
  // RequestPosted 的首包保护（那是算法侧启动瞬间的逻辑）。
  if (header_->state != shm::State::kIdle && header_->state != shm::State::kError) {
    std::ostringstream oss;
    oss << "[shm] 回收非空闲状态再投递 name=" << shm_name_
        << " prev=" << static_cast<int>(header_->state)
        << " seq=" << header_->seq_id;
    LogToStderr(LogSeverity::kWarning, oss.str());
    header_->state = shm::State::kIdle;
    header_->error_message[0] = '\0';
  }

  ++seq_;
  header_->seq_id = seq_;
  header_->station_id = static_cast<std::int32_t>(req.station);
  header_->error_message[0] = '\0';

  std::string write_error;
  if (!shm::WriteRequestToShm(header_, blob_arena_, blob_arena_size_, req, &write_error)) {
    resp->ok = false;
    resp->message = write_error.empty() ? "shm write failed" : write_error;
    header_->state = shm::State::kIdle;
    ReleaseMutex(mtx);
    return false;
  }

  header_->state = shm::State::kRequestPosted;
  const std::uint32_t posted_seq = seq_;
  MemoryBarrier();
  ReleaseMutex(mtx);
  if (evt != nullptr) {
    SetEvent(evt);
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (!IsAlgoProcessAlive()) {
      resp->ok = false;
      resp->message = "algo process dead";
      if (WaitShmMutex(mtx, 500)) {
        if (header_->seq_id == posted_seq) {
          header_->state = shm::State::kIdle;
          std::strncpy(header_->error_message, "process dead", sizeof(header_->error_message) - 1);
        }
        ReleaseMutex(mtx);
      }
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!WaitShmMutex(mtx, 100)) {
      continue;
    }
    const auto st = header_->state;
    if (st == shm::State::kDone) {
      if (header_->seq_id != posted_seq) {
        header_->state = shm::State::kIdle;
        ReleaseMutex(mtx);
        continue;
      }
      for (std::size_t i = 0; i < shm::kLogCount; ++i) {
        resp->logs[i] = FromShm(header_->logs[i]);
      }
      std::string img_err;
      if (shm::ReadFirstResultImage(header_, blob_arena_, blob_arena_size_, &resp->result_image,
                                    &resp->result_image_width, &resp->result_image_height,
                                    &resp->result_image_format, &img_err)) {
        // 可视化图已填入 resp
      }
      resp->ok = true;
      header_->state = shm::State::kIdle;
      ReleaseMutex(mtx);
      return true;
    }
    if (st == shm::State::kError) {
      if (header_->seq_id != posted_seq) {
        header_->state = shm::State::kIdle;
        ReleaseMutex(mtx);
        continue;
      }
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
  if (WaitShmMutex(mtx, 2000)) {
    if (header_->seq_id == posted_seq &&
        (header_->state == shm::State::kRequestPosted || header_->state == shm::State::kBusy ||
         header_->state == shm::State::kDone || header_->state == shm::State::kError)) {
      header_->state = shm::State::kIdle;
      std::strncpy(header_->error_message, "timeout reset", sizeof(header_->error_message) - 1);
    }
    ReleaseMutex(mtx);
  }
  return false;
#else
  (void)req;
  (void)timeout_ms;
  resp->ok = false;
  resp->message = "SHM unsupported on this platform";
  return false;
#endif
}

void ShmAlgoServicePool::Configure(shm::ShmChannelId channel, std::string shm_name,
                                   std::string mutex_name) {
  if (shm_name.empty()) {
    shm_name = shm::ShmNameForChannel(channel);
  }
  if (mutex_name.empty()) {
    mutex_name = shm::MutexNameForChannel(channel);
  }
  auto svc = std::make_shared<ShmAlgoService>(std::move(shm_name), std::move(mutex_name));
  if (channel == shm::ShmChannelId::kR09) {
    r09_ = std::move(svc);
  } else {
    r05_ = std::move(svc);
  }
}

void ShmAlgoServicePool::SetTransferFlags(std::uint32_t transfer_flags) {
  if (r05_) {
    r05_->SetTransferFlags(transfer_flags);
  }
  if (r09_) {
    r09_->SetTransferFlags(transfer_flags);
  }
}

bool ShmAlgoServicePool::Start() {
  bool ok = true;
  if (r05_) {
    ok = r05_->Start() && ok;
  }
  if (r09_) {
    ok = r09_->Start() && ok;
  }
  return ok;
}

void ShmAlgoServicePool::Stop() {
  // 注意：不要在产线 Stop 时拆掉 SHM。映射生命周期应与主程序/算法进程一致。
  // 若此处 Unmap，易与仍在运行的算法进程脱节。真正释放在 ~ShmAlgoService / 进程退出。
}

IAlgoService* ShmAlgoServicePool::TryForStation(StationId station) {
  const shm::ShmChannelId ch = shm::ToShmChannel(station);
  if (ch == shm::ShmChannelId::kR09) {
    return r09_ ? static_cast<IAlgoService*>(r09_.get()) : nullptr;
  }
  return r05_ ? static_cast<IAlgoService*>(r05_.get()) : nullptr;
}

IAlgoService& ShmAlgoServicePool::ForStation(StationId station) {
  IAlgoService* svc = TryForStation(station);
  if (svc != nullptr) {
    return *svc;
  }
  if (r05_) {
    return *r05_;
  }
  if (r09_) {
    return *r09_;
  }
  static MockAlgoService kFallback;
  return kFallback;
}

bool MockAlgoService::Run(const AlgoRequest& req, AlgoResponse* resp, int timeout_ms) {
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
  return true;
}

}  // namespace visual

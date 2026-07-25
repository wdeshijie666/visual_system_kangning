/**
 * @file algo_online_service.cpp
 * @brief 算法进程在线服务：双通道各一线程；PointCloudProcessor 进程内串行化。
 */
#include "algo_online_service.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "algo_input_converter.h"
#include "algo_log.h"
#include "algo_pointcloud_runner.h"
#include "algo_result_io.h"
#include "visual/algo_shm_codec.h"
#include "visual/algo_shm_layout.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace algo {
namespace {

const char* ChannelTag(visual::shm::ShmChannelId channel) {
  return channel == visual::shm::ShmChannelId::kR09 ? "R09" : "R05";
}

void ResolveChannelNames(const AlgoConfig& config, visual::shm::ShmChannelId channel,
                         std::string* shm_name, std::string* mutex_name) {
  const AlgoChannelConfig& ch =
      channel == visual::shm::ShmChannelId::kR09 ? config.channel_r09 : config.channel_r05;
  *shm_name = ch.shm_name.empty() ? visual::shm::ShmNameForChannel(channel) : ch.shm_name;
  *mutex_name = ch.mutex_name.empty() ? visual::shm::MutexNameForChannel(channel) : ch.mutex_name;
}

std::uint32_t TransferFlagsFromConfig(const AlgoConfig& config) {
  std::uint32_t flags = 0;
  if (config.transfer_depth) {
    flags |= static_cast<std::uint32_t>(visual::AlgoTransferFlag::kDepth);
  }
  if (config.transfer_pointcloud) {
    flags |= static_cast<std::uint32_t>(visual::AlgoTransferFlag::kPointCloud);
  }
  if (config.transfer_gray) {
    flags |= static_cast<std::uint32_t>(visual::AlgoTransferFlag::kGray);
  }
  if (flags == 0) {
    flags = static_cast<std::uint32_t>(visual::AlgoTransferFlag::kDepth);
  }
  return flags;
}

#ifdef _WIN32
bool WaitShmMutex(HANDLE mtx, DWORD timeout_ms) {
  const DWORD w = WaitForSingleObject(mtx, timeout_ms);
  return w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
}

void InitHeaderKeepBlob(visual::shm::ShmHeader* header, std::size_t blob_arena_size) {
  std::memset(header, 0, sizeof(visual::shm::ShmHeader));
  header->magic = visual::shm::kMagic;
  header->version = visual::shm::kVersion;
  header->state = visual::shm::State::kIdle;
  header->blob_arena_bytes = static_cast<std::uint64_t>(blob_arena_size);
}

std::size_t ResolveBlobArenaSize(const visual::shm::ShmHeader* header, std::size_t mapped_total) {
  if (mapped_total <= visual::shm::kShmHeaderSize) {
    return 0;
  }
  const std::size_t from_map = mapped_total - visual::shm::kShmHeaderSize;
  if (header != nullptr && header->blob_arena_bytes > 0 &&
      header->blob_arena_bytes <= from_map) {
    return static_cast<std::size_t>(header->blob_arena_bytes);
  }
  return from_map;
}
#endif

}  // namespace

int RunOnlineServiceForChannel(const AlgoConfig& config, visual::shm::ShmChannelId channel,
                               PointCloudProcessorSlot* processor_slot) {
#ifdef _WIN32
  if (channel == visual::shm::ShmChannelId::kR09 && !config.channel_r09.enabled) {
    return 0;
  }
  if (channel == visual::shm::ShmChannelId::kR05 && !config.channel_r05.enabled) {
    return 0;
  }

  std::string shm_name;
  std::string mutex_name;
  ResolveChannelNames(config, channel, &shm_name, &mutex_name);
  const char* tag = ChannelTag(channel);
  const std::uint32_t transfer_flags = TransferFlagsFromConfig(config);
  const std::size_t want_total = visual::shm::ShmTotalSizeForFlags(transfer_flags);

  // 优先 Open：视觉已建好时直接附着，避免 Create 抢先建出空映射
  HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
  DWORD open_err = 0;
  if (!map) {
    open_err = GetLastError();
    map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                             static_cast<DWORD>(want_total), shm_name.c_str());
  }
  if (!map) {
    AlgoError(std::string("[") + tag + "] 共享内存打开/创建失败 openErr=" + std::to_string(open_err) +
              " createErr=" + std::to_string(GetLastError()) + " name=" + shm_name);
    return 1;
  }
  auto* header = static_cast<visual::shm::ShmHeader*>(
      MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!header) {
    AlgoError(std::string("[") + tag + "] 共享内存映射失败 err=" + std::to_string(GetLastError()));
    CloseHandle(map);
    return 1;
  }
  std::size_t mapped_total = want_total;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(header, &mbi, sizeof(mbi)) != 0 && mbi.RegionSize >= visual::shm::kShmHeaderSize) {
    mapped_total = mbi.RegionSize;
  }
  std::size_t blob_arena_size = ResolveBlobArenaSize(header, mapped_total);
  if (blob_arena_size == 0) {
    blob_arena_size = visual::shm::BlobArenaSizeForFlags(transfer_flags);
  }
  auto* blob_arena = visual::shm::BlobArenaBase(header);
  HANDLE mtx = CreateMutexA(nullptr, FALSE, mutex_name.c_str());
  const char* event_name = visual::shm::EventNameForChannel(channel);
  HANDLE evt = CreateEventA(nullptr, FALSE, FALSE, event_name);
  if (!mtx || !evt) {
    AlgoError(std::string("[") + tag + "] 互斥量/事件创建失败");
    return 1;
  }

  // 视觉侧已建好映射时不要整头清零。仅回收崩溃残留 Busy；
  // 勿清 RequestPosted：首包可能在通道线程起来前已投递，清掉会导致视觉侧一直等到超时。
  if (WaitShmMutex(mtx, 5000)) {
    if (header->magic != visual::shm::kMagic || header->version != visual::shm::kVersion) {
      InitHeaderKeepBlob(header, blob_arena_size);
    } else {
      if (header->state == visual::shm::State::kBusy) {
        header->state = visual::shm::State::kIdle;
      }
      if (header->blob_arena_bytes == 0) {
        header->blob_arena_bytes = static_cast<std::uint64_t>(blob_arena_size);
      } else {
        blob_arena_size = static_cast<std::size_t>(header->blob_arena_bytes);
      }
    }
    ReleaseMutex(mtx);
  } else {
    InitHeaderKeepBlob(header, blob_arena_size);
  }

  char module_path[MAX_PATH]{};
  GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  const std::filesystem::path exe_dir = std::filesystem::path(module_path).parent_path();

  {
    std::ostringstream oss;
    oss << "[" << tag << "] 通道已就绪 CHANNEL_READY shm=" << shm_name
        << " attached=" << (open_err == 0 ? 1 : 0) << " arena=" << blob_arena_size << "B";
    AlgoInfo(oss.str());
  }

#if defined(VS_HAS_POINTCLOUD_ALGO)
#else
  (void)processor_slot;
#endif

  while (true) {
    // 事件唤醒（视觉投递后 SetEvent）；超时则继续窥探，避免漏信号
    WaitForSingleObject(evt, 20);

    MemoryBarrier();
    const auto peek =
        *reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<void*>(&header->state));
    if (peek != static_cast<std::uint32_t>(visual::shm::State::kRequestPosted)) {
      continue;
    }

    if (!WaitShmMutex(mtx, 1000)) {
      AlgoWarn(std::string("[") + tag + "] 已见到 Posted 但互斥量等待失败，重试");
      continue;
    }
    if (header->state == visual::shm::State::kRequestPosted) {
      const std::uint32_t work_seq = header->seq_id;
      if (header->blob_arena_bytes > 0 &&
          header->blob_arena_bytes <= mapped_total - visual::shm::kShmHeaderSize) {
        blob_arena_size = static_cast<std::size_t>(header->blob_arena_bytes);
      }
      header->state = visual::shm::State::kBusy;
      const std::int32_t station = header->station_id;
      std::uint32_t depth_w = 0;
      std::uint32_t depth_h = 0;
      if (header->camera_count > 0) {
        depth_w = header->cameras[0].depth.width;
        depth_h = header->cameras[0].depth.height;
      }
      ReleaseMutex(mtx);

      {
        std::ostringstream oss;
        oss << "[" << tag << "] 收到请求 序号=" << work_seq << " 工位=" << station
            << " 深度=" << depth_w << "x" << depth_h << " arena=" << blob_arena_size;
        AlgoInfo(oss.str());
      }

      std::string input_error;
      bool input_ok = false;
      if (header->input_mode == static_cast<std::uint32_t>(visual::AlgoInputMode::kOfflinePath)) {
        input_ok = PrepareAlgoInputFromPaths(header, &input_error);
      } else {
        input_ok = PrepareAlgoInputFromShm(header, blob_arena, blob_arena_size, config, exe_dir,
                                           &input_error);
      }

      if (!WaitShmMutex(mtx, INFINITE)) {
        continue;
      }
      if (header->seq_id != work_seq || header->state != visual::shm::State::kBusy) {
        ReleaseMutex(mtx);
        continue;
      }
      if (!input_ok) {
        std::strncpy(header->error_message, input_error.c_str(), sizeof(header->error_message) - 1);
        header->state = visual::shm::State::kError;
        AlgoError(std::string("[") + tag + "] 输入准备失败: " + input_error);
        ReleaseMutex(mtx);
        continue;
      }
      ReleaseMutex(mtx);

      std::string algo_error;
      if (config.pipeline_simulation.enabled) {
        if (!WaitShmMutex(mtx, INFINITE)) {
          continue;
        }
        if (header->seq_id != work_seq) {
          ReleaseMutex(mtx);
          continue;
        }
        FillPipelineSimulationLogs(header->logs, visual::shm::kLogCount, config.pipeline_simulation);
        AlgoDebug(std::string("[") + tag + "] 使用通路仿真结果");
      } else {
#if defined(VS_HAS_POINTCLOUD_ALGO)
        if (config.use_point_cloud_algo) {
          bool algo_ok = false;
          try {
            // 暂不加全局锁：R05/R09 可并行 process（各通道独立引擎实例）。
            AlgoInfo(std::string("[") + tag + "] 开始计算");
            algo_ok = RunPointCloudFromShm(header, blob_arena, blob_arena_size, config, exe_dir,
                                           header->logs, visual::shm::kLogCount, &algo_error,
                                           processor_slot);
          } catch (const std::exception& ex) {
            algo_ok = false;
            algo_error = std::string("算法异常: ") + ex.what();
          } catch (...) {
            algo_ok = false;
            algo_error = "算法异常";
          }
          if (!WaitShmMutex(mtx, INFINITE)) {
            continue;
          }
          if (header->seq_id != work_seq) {
            ReleaseMutex(mtx);
            continue;
          }
          if (!algo_ok) {
            AlgoError(std::string("[") + tag + "] 计算失败: " + algo_error);
            std::strncpy(header->error_message, algo_error.c_str(),
                         sizeof(header->error_message) - 1);
            header->state = visual::shm::State::kError;
            ReleaseMutex(mtx);
            continue;
          }
        } else {
          if (!WaitShmMutex(mtx, INFINITE)) {
            continue;
          }
          if (header->seq_id != work_seq) {
            ReleaseMutex(mtx);
            continue;
          }
          FillMockLogs(header->logs, visual::shm::kLogCount);
          AlgoDebug(std::string("[") + tag + "] 使用占位结果");
        }
#else
        if (!WaitShmMutex(mtx, INFINITE)) {
          continue;
        }
        if (header->seq_id != work_seq) {
          ReleaseMutex(mtx);
          continue;
        }
        FillMockLogs(header->logs, visual::shm::kLogCount);
        AlgoDebug(std::string("[") + tag + "] 使用占位结果");
#endif
      }

      int ok_n = 0;
      for (std::size_t i = 0; i < visual::shm::kLogCount; ++i) {
        if (header->logs[i].status == 1) {
          ++ok_n;
        }
      }
      {
        std::ostringstream oss;
        oss << "[" << tag << "] 本周期完成 序号=" << header->seq_id << " 合格=" << ok_n << "/"
            << visual::shm::kLogCount;
        AlgoInfo(oss.str());
      }
      header->state = visual::shm::State::kDone;
      ReleaseMutex(mtx);
      continue;
    }
    ReleaseMutex(mtx);
  }
#else
  (void)config;
  (void)channel;
  (void)processor_slot;
  AlgoError("在线模式仅支持 Windows");
  return 1;
#endif
}

int RunOnlineService(const AlgoConfig& config) {
#ifdef _WIN32
  SetAlgoLogLevel(config.log_level);
  AlgoInfo(config.pipeline_simulation.enabled ? "算法服务已启动（通路仿真）"
                                              : "算法服务已启动");
  if (config.debug_save_depth || config.debug_save_pointcloud) {
    AlgoDebug(std::string("调试落盘 深度=") + (config.debug_save_depth ? "开" : "关") +
              " 点云=" + (config.debug_save_pointcloud ? "开" : "关"));
  }

#if defined(VS_HAS_POINTCLOUD_ALGO)
  // R05/R09 各一实例；暂不串行化 process，便于双工位并行压测。
  PointCloudProcessorSlot slot_r05;
  PointCloudProcessorSlot slot_r09;

  std::thread t_r05([&config, &slot_r05]() {
    RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR05, &slot_r05);
  });
  std::thread t_r09([&config, &slot_r09]() {
    RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR09, &slot_r09);
  });
#else
  std::thread t_r05([&config]() {
    RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR05, nullptr);
  });
  std::thread t_r09([&config]() {
    RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR09, nullptr);
  });
#endif
  t_r05.join();
  t_r09.join();
  return 0;
#else
  (void)config;
  AlgoError("在线模式仅支持 Windows");
  return 1;
#endif
}

}  // namespace algo

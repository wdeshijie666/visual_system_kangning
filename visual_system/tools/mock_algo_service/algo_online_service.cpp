/**
 * @file algo_online_service.cpp
 * @brief 算法进程在线服务：双通道各一线程。
 */
#include "algo_online_service.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "algo_input_converter.h"
#include "algo_log.h"
#include "algo_result_io.h"
#include "visual/algo_shm_codec.h"
#include "visual/algo_shm_layout.h"

#if defined(VS_HAS_POINTCLOUD_ALGO)
#include "PointCloudProcessor.h"
#include "algo_pointcloud_runner.h"
#endif

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

}  // namespace

int RunOnlineServiceForChannel(const AlgoConfig& config, visual::shm::ShmChannelId channel) {
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

  HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(visual::shm::kShmTotalSize), shm_name.c_str());
  if (!map) {
    AlgoError(std::string("[") + tag + "] 共享内存创建失败");
    return 1;
  }
  auto* header = static_cast<visual::shm::ShmHeader*>(
      MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, visual::shm::kShmTotalSize));
  auto* blob_arena = visual::shm::BlobArenaBase(header);
  HANDLE mtx = CreateMutexA(nullptr, FALSE, mutex_name.c_str());
  if (!header || !mtx) {
    AlgoError(std::string("[") + tag + "] 共享内存映射失败");
    return 1;
  }
  std::memset(header, 0, visual::shm::kShmTotalSize);
  header->magic = visual::shm::kMagic;
  header->version = visual::shm::kVersion;
  header->state = visual::shm::State::kIdle;

  char module_path[MAX_PATH]{};
  GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  std::filesystem::path exe_dir = std::filesystem::path(module_path).parent_path();

  AlgoInfo(std::string("[") + tag + "] 通道已就绪");

#if defined(VS_HAS_POINTCLOUD_ALGO)
  std::unique_ptr<PointCloudProcessor> processor;
  if (config.use_point_cloud_algo && !config.pipeline_simulation.enabled) {
    const auto cfg_path = [&]() {
      std::filesystem::path p(config.point_cloud_config.empty() ? "config.json"
                                                                : config.point_cloud_config);
      return p.is_absolute() ? p : (exe_dir / p);
    }();
    if (std::filesystem::exists(cfg_path)) {
      processor = std::make_unique<PointCloudProcessor>(cfg_path.string());
      AlgoDebug(std::string("[") + tag + "] 已加载算法配置: " + cfg_path.string());
    } else {
      AlgoError(std::string("[") + tag + "] 算法配置缺失，将使用占位结果: " + cfg_path.string());
    }
  }
#endif

  while (true) {
    if (WaitForSingleObject(mtx, 500) == WAIT_OBJECT_0) {
      if (header->state == visual::shm::State::kRequestPosted) {
        header->state = visual::shm::State::kBusy;
        ReleaseMutex(mtx);

        std::string input_error;
        bool input_ok = false;
        if (header->input_mode == static_cast<std::uint32_t>(visual::AlgoInputMode::kOfflinePath)) {
          input_ok = PrepareAlgoInputFromPaths(header, &input_error);
        } else {
          input_ok = PrepareAlgoInputFromShm(header, blob_arena, visual::shm::kBlobArenaSize, config,
                                             exe_dir, &input_error);
        }

        WaitForSingleObject(mtx, INFINITE);

        if (!input_ok) {
          std::strncpy(header->error_message, input_error.c_str(), sizeof(header->error_message) - 1);
          header->state = visual::shm::State::kError;
          AlgoError(std::string("[") + tag + "] 输入准备失败: " + input_error);
          ReleaseMutex(mtx);
          continue;
        }

        if (config.pipeline_simulation.enabled) {
          FillPipelineSimulationLogs(header->logs, visual::shm::kLogCount, config.pipeline_simulation);
          AlgoDebug(std::string("[") + tag + "] 使用通路仿真结果");
        } else {
#if defined(VS_HAS_POINTCLOUD_ALGO)
          if (config.use_point_cloud_algo) {
            std::string algo_error;
            const bool algo_ok =
                RunPointCloudFromShm(header, blob_arena, visual::shm::kBlobArenaSize, config, exe_dir,
                                     header->logs, visual::shm::kLogCount, &algo_error,
                                     processor.get());
            if (!algo_ok) {
              AlgoError(std::string("[") + tag + "] 计算失败: " + algo_error);
              std::strncpy(header->error_message, algo_error.c_str(),
                           sizeof(header->error_message) - 1);
              header->state = visual::shm::State::kError;
              ReleaseMutex(mtx);
              continue;
            }
          } else {
            FillMockLogs(header->logs, visual::shm::kLogCount);
            AlgoDebug(std::string("[") + tag + "] 使用占位结果");
          }
#else
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
      }
      ReleaseMutex(mtx);
    }
  }
#else
  (void)config;
  (void)channel;
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

  std::thread t_r05([&config]() { RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR05); });
  std::thread t_r09([&config]() { RunOnlineServiceForChannel(config, visual::shm::ShmChannelId::kR09); });
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

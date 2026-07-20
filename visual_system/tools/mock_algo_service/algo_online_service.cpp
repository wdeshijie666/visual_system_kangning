/**
 * @file algo_online_service.cpp
 * @brief 算法进程在线服务：SHM v2 服务端循环（与 ShmAlgoService 配对）。
 */
#include "algo_online_service.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

#include "algo_input_converter.h"
#include "algo_result_io.h"
#include "visual/algo_shm_codec.h"
#include "visual/algo_shm_layout.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace algo {

int RunOnlineService(const AlgoConfig& config) {
#ifdef _WIN32
  HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(visual::shm::kShmTotalSize), visual::shm::kShmName);
  if (!map) {
    std::cerr << "CreateFileMapping failed\n";
    return 1;
  }
  auto* header = static_cast<visual::shm::ShmHeader*>(
      MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, visual::shm::kShmTotalSize));
  auto* blob_arena = visual::shm::BlobArenaBase(header);
  HANDLE mtx = CreateMutexA(nullptr, FALSE, visual::shm::kMutexName);
  if (!header || !mtx) {
    return 1;
  }
  std::memset(header, 0, visual::shm::kShmTotalSize);
  header->magic = visual::shm::kMagic;
  header->version = visual::shm::kVersion;
  header->state = visual::shm::State::kIdle;

  char module_path[MAX_PATH]{};
  GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  std::filesystem::path exe_dir = std::filesystem::path(module_path).parent_path();

  if (config.pipeline_simulation.enabled) {
    std::cout << "mock_algo_service online mode + pipeline simulation (SHM v2)\n";
  } else {
    std::cout << "mock_algo_service online mode running (SHM v2)...\n";
  }
  if (config.debug_save_depth || config.debug_save_pointcloud) {
    std::cout << "debugSave depth=" << (config.debug_save_depth ? "ON" : "OFF")
              << " pointcloud=" << (config.debug_save_pointcloud ? "ON" : "OFF") << " -> "
              << (exe_dir / "data").string() << '\n';
  }

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

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WaitForSingleObject(mtx, INFINITE);

        if (!input_ok) {
          std::strncpy(header->error_message, input_error.c_str(), sizeof(header->error_message) - 1);
          header->state = visual::shm::State::kError;
          ReleaseMutex(mtx);
          continue;
        }

        if (config.pipeline_simulation.enabled) {
          FillPipelineSimulationLogs(header->logs, visual::shm::kLogCount, config.pipeline_simulation);
        } else {
          FillMockLogs(header->logs, visual::shm::kLogCount);
        }
        header->state = visual::shm::State::kDone;
      }
      ReleaseMutex(mtx);
    }
  }
#else
  (void)config;
  std::cerr << "mock_algo_service online mode: Windows only in MVP\n";
  return 1;
#endif
}

}  // namespace algo

#include "algo_offline_replay.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "algo_result_io.h"

namespace algo {
namespace fs = std::filesystem;

bool SessionHasCaptureData(const fs::path& session_dir) {
  if (!fs::exists(session_dir) || !fs::is_directory(session_dir)) {
    return false;
  }
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(session_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.find("_depth.") != std::string::npos || name.find("_rgb.") != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<fs::path> CollectSessionDirs(const fs::path& data_dir) {
  std::vector<fs::path> sessions;
  if (!fs::exists(data_dir)) {
    return sessions;
  }

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(data_dir, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    if (SessionHasCaptureData(entry.path())) {
      sessions.push_back(entry.path());
    }
  }

  if (sessions.empty() && SessionHasCaptureData(data_dir)) {
    sessions.push_back(data_dir);
  }
  std::sort(sessions.begin(), sessions.end());
  return sessions;
}

void AppendOfflineLog(const fs::path& result_dir, const std::string& line) {
  fs::create_directories(result_dir);
  std::ofstream out(result_dir / "offline_replay.log", std::ios::app);
  if (out.is_open()) {
    out << line << '\n';
  }
}

int RunOfflineReplay(const AlgoConfig& config) {
  const fs::path& data_dir = config.offline_replay.data_dir;
  const fs::path& result_dir = config.offline_replay.result_dir;
  fs::create_directories(result_dir);

  const auto sessions = CollectSessionDirs(data_dir);
  if (sessions.empty()) {
    const std::string msg = "offline replay: no session data found in " + data_dir.string();
    std::cerr << msg << '\n';
    AppendOfflineLog(result_dir, msg);
    return 1;
  }

  std::cout << "mock_algo_service offline replay mode\n";
  std::cout << "dataDir=" << data_dir.string() << " resultDir=" << result_dir.string() << '\n';

  int ok_count = 0;
  for (const auto& session : sessions) {
    visual::shm::ShmLogResult logs[visual::shm::kLogCount]{};
    FillMockLogs(logs, visual::shm::kLogCount);

    const std::string session_name = session.filename().string();
    const fs::path out_file = result_dir / (session_name + "_algo_result.txt");
    const bool saved = WriteAlgoResultCsv(out_file, logs, visual::shm::kLogCount);
    const std::string msg = std::string("session=") + session.string() + " saved=" + out_file.string() +
                            (saved ? " ok" : " failed");
    std::cout << msg << '\n';
    AppendOfflineLog(result_dir, msg);
    if (saved) {
      ++ok_count;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  const std::string summary = "offline replay done sessions=" + std::to_string(sessions.size()) +
                              " ok=" + std::to_string(ok_count);
  std::cout << summary << '\n';
  AppendOfflineLog(result_dir, summary);
  return ok_count == static_cast<int>(sessions.size()) ? 0 : 2;
}

}  // namespace algo

/**
 * @file data_recorder.h
 * @brief 数据存根落盘与后台保存（详见 docs/框架流程通路.md §七）。
 *
 * 在线周期：
 *   BuildCaptureRecordContext → AssignCapturePaths → 按 dataStub 写 depth / pointcloud
 *   SaveAlgoResultCsv 在算法返回后同步写结果
 * 回放周期：
 *   BuildReplayRecordContext / FindDepthImageInSession（优先匹配深度图）
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "visual/station_types.h"

namespace visual {

/** 单次视觉周期的落盘上下文。 */
struct CaptureRecordContext {
  std::string output_dir;
  std::string timestamp_prefix;
  std::string station_tag;
};

/** 解析 dataPath（相对路径时基于程序目录）。 */
std::string ResolveDataRoot(const std::string& configured_path);

/** 创建 {dataRoot}/{yyyyMMdd}/ 并生成时间戳前缀 yyyyMMdd_hhmmss。 */
CaptureRecordContext BuildCaptureRecordContext(StationId station, const std::string& configured_data_path);

/** 生成单相机文件前缀：{yyyyMMdd}_{hhmmss}_{station}_{cam_id}。 */
std::string MakeCaptureFilePrefix(const CaptureRecordContext& ctx, const std::string& camera_id);

/** 按 dataStub 开关预生成深度/点云/灰度落盘路径（不写文件）。 */
void AssignCapturePaths(CaptureBundle* bundle, const std::string& session_dir, const std::string& prefix);

/** 将内存中的深度/点云按已赋值路径写入磁盘。 */
bool SaveCaptureBundleToDir(const CaptureBundle& bundle);

/** 保存算法结果为单行 CSV：每条 Log 6 列，共 5 条。result_suffix 默认 algo_result。 */
bool SaveAlgoResultCsv(const CaptureRecordContext& ctx, const LogResultBatch& logs,
                       const std::string& result_suffix = "algo_result");

/** 从历史 session 目录构建落盘上下文（解析已有文件名中的时间戳前缀）。 */
CaptureRecordContext BuildReplayRecordContext(const std::string& session_dir, StationId station);

/** session 目录是否包含采集文件。 */
bool SessionDirHasCaptureData(const std::string& session_dir);

/** 在 session 目录中查找深度图路径（优先匹配 camera_id）。 */
std::string FindDepthImageInSession(const std::string& session_dir, const std::string& station_tag,
                                    const std::string& camera_id = "");

/**
 * 采集落盘后台线程：算法调用与 PLC 写回不等待磁盘 I/O。
 */
class CaptureSaveWorker {
 public:
  static CaptureSaveWorker& Instance();

  void Start();
  void Stop();
  void Enqueue(CaptureBundle bundle);

 private:
  CaptureSaveWorker() = default;
  ~CaptureSaveWorker();

  void WorkerLoop();

  /** 点云异步落盘队列上限，防止磁盘慢时内存无限增长。 */
  static constexpr std::size_t kMaxQueueSize = 8;

  std::atomic<bool> running_{false};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<CaptureBundle> queue_;
};

/**
 * 数据存根保留清理：后台线程轮询，按文件名前缀 yyyyMMdd_hhmmss（秒级）删除超期文件。
 * 默认保留最近 7 天，每小时扫描一次。
 */
class DataStubRetentionCleaner {
 public:
  DataStubRetentionCleaner();
  ~DataStubRetentionCleaner();

  DataStubRetentionCleaner(const DataStubRetentionCleaner&) = delete;
  DataStubRetentionCleaner& operator=(const DataStubRetentionCleaner&) = delete;

  void Start(const std::string& configured_data_path, int retention_days = 7, int poll_interval_sec = 3600);
  void Stop();

 private:
  void WorkerLoop();
  std::size_t PurgeExpiredStubsOnce(const std::string& configured_data_path, int retention_days) const;

  std::atomic<bool> running_{false};
  std::thread worker_;
  std::string data_path_;
  int retention_days_ = 7;
  int poll_interval_sec_ = 3600;
};

}  // namespace visual

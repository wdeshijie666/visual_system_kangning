# 双工位并行方案：双 SHM + 双算法线程

> 状态：**已实施（双 SHM + 双执行线程 + 算法双线程）**  
> 前置阅读：[框架流程通路.md](./框架流程通路.md)  
> 目标：R05 与 R09 可同时各跑一套完整视觉周期（采集 → 算法 → 写 PLC），互不阻塞、互不踩共享内存。

---

## 1. 背景与动机

### 1.1 当前限制（v2 单通道）

| 问题 | 现状 |
|------|------|
| 编排 | `SequenceEngine::WorkerLoop` 单线程，`RunCycle` 同步阻塞 |
| 触发互斥 | 同 tick 内 `if (edge_r09) … else if (edge_r05)`，R09 优先，R05 可能丢触发 |
| SHM | 全局一块 `Local\VisualSystemAlgo_v2` + 一把 `Mutex_v2` |
| 状态机 | `ShmHeader` 仅一套 `state` / `logs` / `station_id` |
| 算法进程 | `mock_algo_service` 单循环，一次处理一个 `kRequestPosted` |
| 跨线程 | UI `RunOfflineCycle` / `RunReplayCycle` 与 Worker **无互斥**，可并发踩 SHM |

**结论**：当前仅适合「双工位不同时跑」；不支持两工位同时在跑。

### 1.2 本方案要达成

- R05（含 R07 映射）与 R09 **并行**执行完整周期。
- 每工位 **独立 SHM 映射 + 独立命名互斥量**，视觉写、算法读互不覆盖。
- 算法进程内 **两条服务线程**，各绑定一个工位 SHM，真正并行计算（需算法 SDK 线程安全或每线程独立上下文）。
- 编排层 **工位级执行单元**（队列 + 线程或任务调度），消除 `if/else` 丢触发。
- **线程安全**：视觉侧、UI 侧、落盘后台、PLC 写入均有明确边界。

### 1.3 非目标（本期不做）

- 单工位内多相机并行算法（仍是一次 `AlgoRequest` 多 `captures`）。
- 单 SHM 内双槽位分区（方案 B，见 §8 备选）。
- 每工位独立算法 **进程**（可后续扩展，本期为 **单进程双线程**）。
- R07 作为与 R05 独立的第三路 SHM（R07 仍映射到 R05 通道，与 PLC V0.2 一致）。

---

## 2. 总体架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ VisualSystem.exe                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│  StationRunner R05                    StationRunner R09                    │
│    队列 + 工作线程（或共享池）              队列 + 工作线程                      │
│    PLC 边沿 / 离线入口                    PLC 边沿 / 离线入口                  │
│         │                                      │                             │
│         ▼                                      ▼                             │
│    RunCycle(R05)                          RunCycle(R09)                      │
│         │                                      │                             │
│         ▼                                      ▼                             │
│  ShmAlgoService(R05)                    ShmAlgoService(R09)                 │
│  SHM: ...Algo_R05_v3                  SHM: ...Algo_R09_v3                    │
└─────────┬────────────────────────────────────┬────────────────────────────┘
          │                                    │
          ▼                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ mock_algo_service.exe（单进程）                                               │
│   Thread-R05: RunOnlineServiceForChannel(R05 shm/mutex)                      │
│   Thread-R09: RunOnlineServiceForChannel(R09 shm/mutex)                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

**与现网差异**：由「1 映射 + 1 状态机 + 1 算法循环」变为「2 映射 + 2 状态机 + 2 算法线程」。

---

## 3. 共享内存与互斥量命名（v3）

### 3.1 命名约定

| 工位逻辑通道 | SHM 映射名 | 互斥量名 | 说明 |
|--------------|------------|----------|------|
| R05（含 R07） | `Local\VisualSystemAlgo_R05_v3` | `Local\VisualSystemAlgoMutex_R05_v3` | `station_r05` 相机、R05+R07 PLC 写回 |
| R09 | `Local\VisualSystemAlgo_R09_v3` | `Local\VisualSystemAlgoMutex_R09_v3` | `station_r09` 相机、R09 PLC 写回 |

- 版本号升至 **v3**，与现有 v2 单通道 **并存期可共存**，便于灰度；上线后废弃 v2 常量。
- 每块 SHM 大小仍为 `kShmTotalSize = kShmHeaderSize + kBlobArenaSize`（与 v2 布局相同，**每工位各分配一整块**）。
- 总内存（两工位）：`2 × kShmTotalSize`（约数百 MB 量级，与当前单块相同数量级 ×2）。

### 3.2 布局（沿用 v2 结构，不改字段）

每工位一块内存，布局不变：

```
[ ShmHeader ][ blob arena (kBlobArenaSize) ]
```

- `ShmHeader::station_id` 仍填写请求工位，便于算法日志与校验。
- 每通道只有本工位相机，`camera_count` 通常为 1。
- `algo_shm_layout.h`：保留结构体；新增 `ShmChannelId` / 命名工厂函数，而非单一 `kShmName`。

### 3.3 命名工厂（建议 API）

```cpp
// algo_shm_layout.h（拟新增）
enum class ShmChannelId : std::uint8_t { kR05 = 5, kR09 = 9 };

const char* ShmNameForChannel(ShmChannelId channel);
const char* MutexNameForChannel(ShmChannelId channel);

// 由 StationId 映射：R05/R07 → kR05，R09 → kR09
ShmChannelId ToShmChannel(StationId station);
```

---

## 4. 配置变更（setting.json）

### 4.1 推荐结构

```json
"algo": {
  "useShm": true,
  "timeoutMs": 30000,
  "transferDepth": true,
  "transferPointcloud": true,
  "programDir": "alg_program",
  "programExe": "mock_algo_service.exe",
  "channels": {
    "r05": {
      "shmName": "Local\\VisualSystemAlgo_R05_v3",
      "mutexName": "Local\\VisualSystemAlgoMutex_R05_v3"
    },
    "r09": {
      "shmName": "Local\\VisualSystemAlgo_R09_v3",
      "mutexName": "Local\\VisualSystemAlgoMutex_R09_v3"
    }
  },
  "legacyShmName": "Local\\VisualSystemAlgo_v2"
}
```

- `legacyShmName`：迁移期只读兼容；实施完成后删除。
- 若省略 `channels`，代码回退到 `ShmNameForChannel` 默认常量。

### 4.2 AppSettings 扩展

```cpp
struct AlgoChannelSettings {
  std::string shm_name;
  std::string mutex_name;
};

struct AppSettings {
  // ...
  AlgoChannelSettings algo_channel_r05;
  AlgoChannelSettings algo_channel_r09;
};
```

---

## 5. 视觉侧设计

### 5.1 ShmAlgoService → 按工位实例化

**现状**：`main.cpp` 创建一个 `ShmAlgoService(settings.algo_shm_name)` 注入 `SequenceEngine`。

**目标**：

```cpp
class ShmAlgoServicePool {
 public:
  void Configure(ShmChannelId ch, std::string shm_name, std::string mutex_name);
  bool Start();  // 映射所有已配置通道
  void Stop();
  IAlgoService& ForStation(StationId station);  // 内部 ToShmChannel
};
```

- 每个通道独立：`mapping_` / `header_` / `blob_arena_` / `mutex_` / `seq_`。
- `ShmAlgoService::Run` 逻辑基本不变，仅 mutex 名从构造参数读取。
- `MockAlgoService`（`useShm=false`）可保留单实例或同样按工位隔离（低优先级）。

### 5.2 SequenceEngine 并行编排

#### 方案 5.2.1：双 Worker 线程（推荐，与双 SHM 对称）

```
WorkerLoopR05: 只 Poll R05/R07 边沿 → 入队或直接 RunCycle(R05)
WorkerLoopR09: 只 Poll R09 边沿     → 入队或直接 RunCycle(R09)
```

| 项 | 说明 |
|----|------|
| 线程数 | +1（共 2 个 PLC 轮询线程，或 1 个 Poll 线程 + 2 个执行线程） |
| `RunCycle` | 每工位独立调用，**禁止**共享可变静态状态 |
| PLC | `VisionPlcAdapter` 需确认 `PollTrigger` / `WriteLogResults` 线程安全；若不安全，PLC 访问加 `plc_mutex_` |
| 相机 | R05 通道只用 `rvc_01`，R09 只用 `rvc_02`，按 `station_cfg` 取，无交叉 |

#### 方案 5.2.2：单 Poll 线程 + 双任务队列

- Poll 线程只负责边沿检测，将 `(StationId, timestamp)` 推入 `queue_r05` / `queue_r09`。
- 两个 `std::thread` 消费者各处理一个队列，**避免 if/else 丢触发**。
- 队列上限与溢出策略：建议 `max_depth=2`，满则记日志 + `EventBus` 告警（PLC 侧重发或人工介入）。

#### 离线 / 回放入口

```cpp
// 所有进入 RunCycle 的路径必须：
// 1) 根据 station 选择对应 ShmAlgoService
// 2) 经 StationExecutor 派发，避免与 Worker 并发踩同一工位资源

std::mutex cycle_mutex_r05_;
std::mutex cycle_mutex_r09_;
// RunCycle 开头：lock_guard lock(ForStation(station))  // 同工位互斥，异工位并行
```

- **同工位**：Worker 与 UI 离线测试 **互斥**（防止双线程同时 Capture 同一相机）。
- **异工位**：可并行。

### 5.3 RunCycle 内算法调用变更

```cpp
// 伪代码
auto& algo = algo_pool_.ForStation(station);
algo_ok = algo.Run(req, &algo_resp, settings.algo_timeout_ms);
```

- `AlgoRequest::station` 不变；SHM 通道由 `station` 推导，**不再**多工位共用一个 `IAlgoService`。

### 5.4 CaptureSaveWorker

- 已为线程安全队列，**无需双实例**；`Enqueue` 时 bundle 自带路径前缀，R05/R09 自然隔离。
- 注意：`CaptureBundle` 内 `shared_ptr` 缓冲区在 `Enqueue` 后由 worker 消费，调用方不得再改。

### 5.5 EventBus / UI

- `CycleCompleted` 已带 `station`，UI 侧无需改架构。
- 设备状态：算法进程仍 **一个** QProcess，状态可显示「R05/R09 通道运行中」（由算法 stdout 或扩展心跳上报）。

---

## 6. 算法侧设计（双线程）

### 6.1 进程模型

- **仍启动一个** `mock_algo_service.exe`（`AlgoProcessManager` 不变）。
- `main()` 启动后创建两条线程：

```cpp
std::thread t_r05(RunOnlineServiceForChannel, config, ShmChannelId::kR05);
std::thread t_r09(RunOnlineServiceForChannel, config, ShmChannelId::kR09);
t_r05.join();  // 进程常驻
```

### 6.2 `RunOnlineServiceForChannel`

从现有 `RunOnlineService` 抽取：

| 参数 | 说明 |
|------|------|
| `shm_name` / `mutex_name` | 来自配置或 `ShmNameForChannel` |
| 循环体 | 与现 v2 相同：`kRequestPosted` → `kBusy` → 转换输入 → 写 `logs` → `kDone` |
| 日志前缀 | `[R05]` / `[R09]` 便于 `algo_process.log` 区分 |

### 6.3 算法 SDK 线程安全（实施前必须确认）

| 情况 | 处理 |
|------|------|
| SDK 可重入 / 无全局状态 | 两线程各算各的，直接并行 |
| SDK 非线程安全 | 每线程 `thread_local` 上下文，或算法内对 SDK 加 `sdk_mutex`（会部分抵消并行收益） |
| 仅支持单实例 | 退化为「双 SHM + 算法侧请求队列单线程」（见 §8 备选 C） |

当前 `mock_algo_service` 为桩，**并行无风险**；接入真实算法时必须评估。

### 6.4 algo_config.json 扩展（可选）

```json
{
  "mode": 0,
  "channels": {
    "r05": { "enabled": true },
    "r09": { "enabled": true }
  },
  "pipelineSimulation": { ... }
}
```

---

## 7. 线程安全与内存分配清单

### 7.1 资源矩阵

| 资源 | R05 线程 | R09 线程 | 保护方式 |
|------|----------|----------|----------|
| SHM R05 | 读/写 | 不访问 | 通道 Mutex R05 |
| SHM R09 | 不访问 | 读/写 | 通道 Mutex R09 |
| 相机 rvc_01 | Capture | 不用 | `cycle_mutex_r05` |
| 相机 rvc_02 | 不用 | Capture | `cycle_mutex_r09` |
| PLC 驱动 | 读/写 R05/R07 | 读/写 R09 | `plc_mutex`（若驱动非线程安全） |
| CaptureSaveWorker | 共用 | 共用 | 内部 `queue_mutex` |
| EventBus | 发射 | 发射 | Qt 信号跨线程排队 |

### 7.2 内存分配

| 分配点 | 次数 | 说明 |
|--------|------|------|
| SHM 映射 | 视觉 2 + 算法 2（同物理页，OS 共享） | 每通道 `kShmTotalSize` |
| `Capture()` buffer | 每周期每相机堆上 `vector` | 与现网相同；并行时峰值 ×2 |
| SHM blob 拷贝 | `WriteRequestToShm` | 每通道独立 arena，无交叉 |

### 7.3 禁止事项

- 禁止两工位共用同一个 `ShmAlgoService` 实例。
- 禁止在无锁情况下从 UI 线程与 Worker 同时对**同一工位**调用 `RunCycle`。
- 禁止算法线程跨通道读取对方 `header_` 指针。

---

## 8. 备选方案（本期不实施，备查）

| 方案 | 概要 | 适用 |
|------|------|------|
| B 单 SHM 双槽 | `[Header5][Blob5][Header9][Blob9]` | 想少一个映射名 |
| C 双 SHM + 算法单线程队列 | 视觉并行，算法 FIFO | SDK 只能单线程 |
| D 双进程双 EXE | 两个 `mock_algo_service` | 强隔离，运维成本高 |

**选定**：**方案 A（双 SHM + 双算法线程）**，与产线双工位对称、边界最清晰。

---

## 9. 实施步骤（建议顺序）

### Phase 1 — 协议与命名（无行为变化）

1. `algo_shm_layout.h`：增加 `ShmChannelId`、`ShmNameForChannel`、`MutexNameForChannel`、`ToShmChannel`。
2. `ShmAlgoService`：构造函数增加 `mutex_name` 参数；`EnsureMapping` 使用独立 mutex 名。
3. `setting.json` / `AppContext`：解析 `algo.channels.r05/r09`。
4. 文档：`algo_shm_protocol.md` 增加 v3 双通道章节。

### Phase 2 — 视觉侧双实例

1. 实现 `ShmAlgoServicePool`（或两个 `shared_ptr<ShmAlgoService>` 注入 `SequenceEngine`）。
2. `RunCycle` 按 `station` 选择 algo 实例。
3. `main.cpp` 创建 pool 并 `Start()` 两块映射。

### Phase 3 — 编排并行

1. `SequenceEngine`：拆 `WorkerLoop` 为双通道，或 Poll + 双队列 + 双执行线程。
2. 增加 `cycle_mutex_r05_` / `cycle_mutex_r09_`；`RunOfflineCycle` / `RunReplayCycle` 走同一派发层。
3. 去掉 `if (edge_r09) … else if (edge_r05)`，改为独立边沿处理。
4. `VisionPlcAdapter`：评估并加 `plc_mutex_`（如需）。

### Phase 4 — 算法侧双线程

1. 抽取 `RunOnlineServiceForChannel`。
2. `main.cpp` 启动 R05/R09 两线程；日志加通道前缀。
3. 验证两通道同时 `kBusy` 互不干扰。

### Phase 5 — 联调与收尾

1. 仿真模式：两工位同时离线触发 / 模拟 PLC 边沿。
2. 压力：R05、R09 周期重叠，检查 SHM seq_id、PLC 写回、落盘路径。
3. 废弃 `algo.shmName` 单通道配置；删除 v2 默认映射（或保留一版兼容开关）。

---

## 10. 涉及文件清单

| 文件 | 变更 |
|------|------|
| `libs/adapter_algo_shm/include/visual/algo_shm_layout.h` | 通道枚举、命名工厂 |
| `libs/adapter_algo_shm/include/visual/shm_algo_service.h` | mutex 名、Pool |
| `libs/adapter_algo_shm/src/shm_algo_service.cpp` | 多通道映射 |
| `libs/app_core/include/visual/app_context.h` | `AlgoChannelSettings` |
| `libs/app_core/src/app_context.cpp` | 解析 channels |
| `libs/app_core/include/visual/sequence_engine.h` | 双 Worker / 队列 / 互斥 |
| `libs/app_core/src/sequence_engine.cpp` | 并行编排 |
| `libs/adapter_plc/src/vision_plc_adapter.cpp` | 可选 `plc_mutex_` |
| `apps/visual_system/src/main.cpp` | 创建 Algo Pool |
| `tools/mock_algo_service/main.cpp` | 双线程启动 |
| `tools/mock_algo_service/algo_online_service.cpp/.h` | `RunOnlineServiceForChannel` |
| `config/setting.json` | channels 配置 |
| `docs/algo_shm_protocol.md` | v3 双通道 |
| `docs/框架流程通路.md` | 更新并行架构图 |

---

## 11. 测试用例

| # | 场景 | 期望 |
|---|------|------|
| T1 | 仅 R05 触发 | 只写 R05 SHM，R09 SHM 保持 `kIdle` |
| T2 | 仅 R09 触发 | 相反 |
| T3 | R05、R09 同时触发 | 两 `RunCycle` 并行；两 SHM 各走完整状态机；PLC 各写各工位 |
| T4 | R05 算法耗时 > R09 周期 | R09 不等待 R05（两通道独立） |
| T5 | UI 离线 R05 + Worker R09 同时 | 异工位并行；同工位 UI+Worker 串行（mutex） |
| T6 | 回放 R09 + 在线 R05 | `kOfflinePath` 与 `kOnlineShm` 在不同 SHM，互不影响 |
| T7 | 算法进程重启 | 两通道映射重新 `EnsureMapping`，首请求初始化 magic/version |

---

## 12. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 真实算法 SDK 非线程安全 | 实施前评估；必要时算法侧 `sdk_mutex` 或退方案 C |
| PLC 驱动非线程安全 | `VisionPlcAdapter` 全局互斥 |
| 内存峰值翻倍 | 可配置关闭 `transferPointcloud` 或降低 `kMaxImageWidth/Height` |
| v2 与 v3 混跑 | 升级窗口内统一配置；算法与视觉必须同版本 |
| 触发风暴撑爆队列 | 队列深度限制 + 告警 + PLC 握手「忙碌」位（后续协议） |

---

## 13. 验收标准

1. R05、R09 各触发一次且时间重叠时，两路均在 `timeoutMs` 内返回，且 `logs` 写入正确 PLC 区。
2. `Process Explorer` / 日志可见 **两个** 命名 SHM 区域均有活动。
3. 算法日志同时出现 `[R05]` 与 `[R09]` 处理记录，无交叉 `seq_id` 覆盖。
4. 单工位行为与现网 v2 一致（回归离线测试、历史回放）。
5. 文档与 `setting.json` 示例已更新，实施可按 Phase 1→5  checklist 执行。

---

## 14. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-06-20 | 初稿：双 SHM + 双算法线程，基于 v2 单通道现状分析 |
| 2026-07-21 | 落地：双通道命名、ShmAlgoServicePool、双队列双 Worker、算法双线程、PLC 入口锁 |

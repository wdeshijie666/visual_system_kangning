# 接入真机 AB PLC 与 MemoryTransport 仿真触发

## 一、真机 AB PLC（除 `VISION_PLC_ENABLE_LIBPLCTAG=ON` 外）

### 1. 构建

当前 `visual_system/CMakeLists.txt` 中 `VISION_PLC_ENABLE_LIBPLCTAG` **已不再 FORCE**，重新配置即可生效：

```bat
cmake -S visual_system -B visual_system/build ^
  -DTHIRD_PARTY_LIBRARY_DIR=G:/3rdParty ^
  -DVISION_PLC_ENABLE_LIBPLCTAG=ON ^
  -DRVC_ROOT="D:/Program Files/RVBUST/RVC/RVCSDK"
cmake --build visual_system/build --config Release --target VisualSystem
```

依赖（`THIRD_PARTY_LIBRARY_DIR`，默认 `G:/3rdParty`）：

| 文件 | 用途 |
|------|------|
| `include/libplctag.h` | 头文件 |
| `lib/plctag.lib` | 链接 |
| `bin/plctag.dll` | 运行时（POST_BUILD 拷到 exe 旁） |

定义宏 `VISION_PLC_HAS_LIBPLCTAG` 后，`VisionPlcAdapter` 使用 `CreateLibPlcTagTransport()`，不再走 MemoryTransport。

### 2. `config/setting.json`

```json
"plc": {
  "gateway": "192.168.1.10",
  "path": "1,0",
  "tags": {
    "cameraToPlc": "CameraToPLC_Int",
    "plcToCamera": "PLCToCamera_Int"
  }
}
```

| 字段 | 说明 | 典型值 |
|------|------|--------|
| `gateway` | PLC IP（**必填非空**，否则 `TryConnectPlc` 失败） | `192.168.1.10` |
| `path` | CIP 路径（背板+槽位） | `1,0` |
| `tags.cameraToPlc` | 视觉→PLC DINT[100] | `CameraToPLC_Int` |
| `tags.plcToCamera` | PLC→视觉 DINT[100] | `PLCToCamera_Int` |

`timeout_ms` 代码默认 5000，暂未从 JSON 读取。

真机时请关闭或忽略仿真触发字段（见下节）；有 `VISION_PLC_HAS_LIBPLCTAG` 时不会启动注入线程。

### 3. PLC 程序侧

- Controller Tag：`CameraToPLC_Int`、`PLCToCamera_Int`，类型 **DINT[100]**（Logix / `cpu=lgx`）。
- 位与结果区见 [`plc_field_map.md`](./plc_field_map.md)（触发 `[0].1`/`.2`，结果 R05=10..39、R07=40..69、R09=70..99）。
- PC ↔ PLC EtherNet/IP 互通；防火墙放行。
- 协议：视觉写心跳 `[0].0`（2s）；PLC 负责除心跳外对 `CameraToPLC` 清零（按接口约定）。

### 4. 联调

1. 可用 `AB_PLC/examples/vision_plc_real_example` 先独立验证读写。
2. 启动 VisualSystem → **启动** → 看 PLC 状态灯与心跳。
3. PLC 置触发位 → 视觉采图/算法 → 结果区与完成位更新。

---

## 二、MemoryTransport 循环触发（默认 30s）

仅在 **未** 编译 `VISION_PLC_HAS_LIBPLCTAG`（Memory 仿真）时生效。

### 配置

```json
"plc": {
  "gateway": "192.168.1.10",
  "path": "1,0",
  "simAutoTrigger": true,
  "simAutoTriggerIntervalSec": 30,
  "tags": { ... }
}
```

| 字段 | 默认 | 含义 |
|------|------|------|
| `simAutoTrigger` | `true` | 产线 Start 后自动注入触发 |
| `simAutoTriggerIntervalSec` | `30` | 两次注入间隔（秒） |

### 行为

1. 点 **启动** → `StartHeartbeat` → 启动注入线程。
2. 约 1s 后首次注入 R05，之后每 N 秒轮流注入：**R05 → R09 → R05 → …**。
3. 引擎 `PollLoop` 上升沿 → `RunCycle` → 写结果。
4. `WriteLogResults` / `WriteSequenceCompleted` 时向 **stderr** 打印，例如：

```text
[PLC仿真] 注入触发 R05（间隔 30s）
[PLC仿真] 收到视觉结果 station=R05
  Log1 status=1 X=... Y=... R=... D=... L=...
...
[PLC仿真] SequenceCompleted station=R05 completed=1
```

5. 点 **停止** → `StopHeartbeat` 结束注入。

关闭自动触发：`"simAutoTrigger": false`（仍可用手动触发）。

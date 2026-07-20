# PLC 字段对照（CameraPC_Interface V0.2）

依据 `Auto_Cutting2_PLC_Robot_Interface20260612_V0.2.xlsx` 与 `AB_PLC/libs/vision_plc_driver/include/vision_plc/protocol_constants.h`。

## Tag 模型

| Tag 名（默认） | 方向 | 类型 | 长度 |
|----------------|------|------|------|
| `PLCToCamera_Int` | PLC → 视觉 | DINT 数组 | [0..99] |
| `CameraToPLC_Int` | 视觉 → PLC | DINT 数组 | [0..99] |

Tag 名可在 `config/setting.json` → `plc.tags` 覆盖。

## 控制字 PLCToCamera_Int[0] / CameraToPLC_Int[0]

| 位 | 含义 |
|----|------|
| `[0].0` | 心跳（视觉写 CameraToPLC，每 2s 0/1 跳变） |
| `[0].1` | R05/R07 触发（PLC 写）/ 视觉序列完成（视觉写） |
| `[0].2` | R09 触发 / R09 视觉序列完成 |

### 触发（PollTrigger）

| 工位 | 读取 Tag | 位 |
|------|----------|-----|
| R05 | PLCToCamera_Int | [0].1 |
| R07 | PLCToCamera_Int | [0].1（与 R05 同触发位） |
| R09 | PLCToCamera_Int | [0].2 |

### 完成（WriteSequenceCompleted）

| 工位 | 写入 Tag | 位 |
|------|----------|-----|
| R05 | CameraToPLC_Int | [0].1 |
| R07 | CameraToPLC_Int | [0].1 |
| R09 | CameraToPLC_Int | [0].2 |

## Log 结果区 CameraToPLC_Int

每工位 **5 根 Log**，每根 **6 个 DINT 字段**（Status, X, Y, R, Diameter, Length）。

| 工位 | 起始下标 | 结束下标 | Log 编号 |
|------|----------|----------|----------|
| R05 | 10 | 39 | Log1~5 |
| R07 | 40 | 69 | Log1~5 |
| R09 | 70 | 99 | Log1~5 |

### 单根 Log 字段偏移（相对该 Log 块起点）

| 偏移 | 字段 | 单位/刻度 |
|------|------|-----------|
| +0 | Status | 0=Default, 1=OK, 2=NG, 3=NotLog |
| +1 | Offset X | mm × 100（整数 DINT） |
| +2 | Offset Y | mm × 100 |
| +3 | Offset R | deg × 100 |
| +4 | Diameter | mm × 10 |
| +5 | Length | mm × 10 |

### 示例：R05 Log3 Status

- 数组下标 = `10 + (3-1)*6 + 0` = **22**

### 示例：R09 Log5 Length

- 起始 70，Log5 块起点 = `70 + 4*6` = 94，Length 偏移 +5 → 下标 **99**

## VisualSystem 映射

`VisionPlcAdapter` 将 `visual::LogResultBatch`（5 条 `LogResult`）转为 `vision_plc::VisionLogResultBatch`，由 `VisionPlcDriver::WriteLogResults` 按上表刻度写入。

`SequenceEngine` 在 R05 工位周期完成后对 **R05 与 R07** 各写一份 batch（当前默认策略；实机可与 PLC 程序确认）。

## 连接参数

`config/setting.json`：

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

对应 libplctag：`Connect({ gateway, path })`。

## 协议约定摘要

1. 视觉负责心跳 `[0].0`，PLC 负责除心跳外 CameraToPLC 数据清零。
2. 一次触发 → 一次采集/算法 → 写 5 条 Log → 置完成位。
3. R05/R07 结果区分开存储，触发可共用 `[0].1`。

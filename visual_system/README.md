# VisualSystem

工业视觉主控软件：AB PLC V0.2 + RVC 3D 相机 + 算法 SHM/Mock + RVCManager 风格 UI。

## 构建

默认三方库根目录 `G:/3rdParty`；RVC 相机 SDK 默认路径如下（CMake 可覆盖）：

| 变量 | 默认路径 |
|------|----------|
| `RVC_ROOT` | `D:/Program Files/RVBUST/RVC/RVCSDK` |
| `RVC_LIB_DIR` | `${RVC_ROOT}/lib`（含 `RVC.lib`） |
| `RVC_RUNTIME_DIR` | `${RVC_ROOT}/runtime`（含 `RVC.dll`） |

```bat
cmake -S . -B build ^
  -DTHIRD_PARTY_LIBRARY_DIR=G:/3rdParty ^
  -DVISION_PLC_ENABLE_LIBPLCTAG=ON ^
  -DRVC_ROOT="D:/Program Files/RVBUST/RVC/RVCSDK"
cmake --build build --config Release
```

若检测到 `RVC.lib` 与头文件，将链接真实 RVC 适配器；否则回退 Stub 相机。构建成功后 `RVC.dll` 会拷贝到 exe 同目录。

输出：

- `build/apps/visual_system/Release/VisualSystem.exe`
- `build/tools/mock_algo_service/Release/mock_algo_service.exe`

## 运行

1. 编辑运行目录下 `config/setting.json`（构建后自动拷贝）。
2. 启动 `VisualSystem.exe`，工具栏点 **启动** 开始 PLC 轮询与心跳。
3. 离线测试：状态栏 **离线跑 R05/R09 工位**。
4. 使用 SHM 算法时：先启动 `mock_algo_service.exe`，`setting.json` 设 `"useShm": true`。

## 文档

见 `docs/` 目录。

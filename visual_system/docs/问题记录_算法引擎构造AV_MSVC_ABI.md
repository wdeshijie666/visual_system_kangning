# 问题记录：真机算法引擎构造访问冲突（MSVC ABI / CRT）

> 日期：2026-07-24  
> 状态：**已关闭**（2026-07-24 后：`PointCloudProcessor.dll` 已用 **v142** 重编；仓库已**移除** `pcp_c_api` 桥接，算法进程直接链接引擎）  
> 影响模块：`alg_program` / `mock_algo_service` / `PointCloudProcessor.dll`

---

## 0. 当前结论（先看这里）

| 项 | 说明 |
|----|------|
| 根因 | 当时引擎为 VS2022(**14.44**)，算法进程为 VS2019(**14.29**)，跨 DLL 传 `std::string` 等 C++ 对象导致构造 AV；旁路 VC142 CRT 会加剧问题 |
| 临时方案（已废弃） | `pcp_c_api` C ABI 桥 + 强制部署 VC143 CRT |
| **现行方案** | **引擎与 `mock_algo_service` 同为 v142**，直接 `new PointCloudProcessor`；部署旁勿混用另一工具集的 CRT |

以下正文保留事故经过与排查要点，便于以后再踩工具集不一致时对照。

---

## 1. 现象

真机模式下算法通道可就绪（`CHANNEL_READY`），但首次计算失败，日志类似：

```text
[R05] 开始计算
[R05] 计算失败: 算法引擎构造失败(原生异常/访问冲突)，请确认 alg_program 依赖 DLL 完整且勿混用管理员/普通权限
周期结束 模式=实机 ... 算法=失败 PLC=成功
```

特征：

- 仿真模式往往正常（不构造真实 `PointCloudProcessor`）。
- 共享内存、采图、PLC 回报可正常；失败集中在**算法引擎构造**。
- 客户机更容易复现；开发机「以前能跑」不能证明当前 DLL 组合安全。

---

## 2. 根因

本质是 **MSVC 工具集不一致**，叠加 **旁路 CRT 加载优先级**，不是缺「VC++ 运行库安装」或管理员权限问题。

### 2.1 跨工具集直接调用 C++ 接口（主因）

| 模块 | 典型链接器版本 |
|------|----------------|
| `PointCloudProcessor.dll`（ReconDLL） | **14.44**（VS2022 / v143） |
| `mock_algo_service.exe`（本工程） | **14.29**（VS2019 / v142） |

算法进程曾直接执行：

```cpp
new PointCloudProcessor(const std::string& config_path);
```

`std::string` 等 C++ 对象跨 DLL、跨 MSVC 工具集传递属于未定义行为，常见表现即为构造期 **ACCESS_VIOLATION**（被 `/EHa` 捕获后打出上述日志）。

本地用 VS2019 编译的探测程序调用同一份 14.44 `PointCloudProcessor.dll` 可稳定复现；改用 VS2022 编译同一探测程序则构造成功。

### 2.2 旁路 VC142 CRT 覆盖系统 CRT（次因 / 放大器）

部署目录若自带旧版：

- `msvcp140.dll`
- `vcruntime140.dll`
- `vcruntime140_1.dll`

Windows 会**优先加载 exe 旁 DLL**，而不是系统里已安装的较新 VC++ 红包。

对 14.44 编译的 `PointCloudProcessor` / `pcp_c_api` 而言，旁路 VC142 CRT 会导致同样在构造期 AV。  
本地对照实验：

| 环境 | `pcp_create(config)` |
|------|----------------------|
| 无旁路 CRT（用系统 CRT） | OK |
| 旁路 VC142 CRT | FAIL（AV） |
| 旁路 VC143 CRT | OK |

因此：**仅在客户机安装 VC++ 运行库不能解决本次问题**（旁路旧 CRT 仍优先；且 C++ ABI 不匹配也无法靠红包修复）。

### 2.3 为何开发机「以前真机能跑」

常见解释（可并存）：

1. 当时使用的 `PointCloudProcessor.dll` 与调用方**同工具集**（例如均为 v142）。
2. 真机流程未真正进入引擎构造（仿真跳过、配置未启用真实点云等）。
3. 开发机未把 VC142 CRT 打进 `alg_program` 旁路，加载到的是较新系统 CRT，表现与客户包不一致。

「本机曾成功」≠「跨 14.29↔14.44 直接调 C++ 安全」。

---

## 3. 当时的临时方案（已废弃，勿再部署）

> **勿再使用**：`pcp_c_api` 源码已从仓库删除；算法进程恢复直接链接 `PointCloudProcessor`。

### 3.1（历史）C ABI 桥接 DLL

曾新增 `pcp_c_api`（VS2022 编），用纯 C 接口隔离工具集差异；`mock_algo_service` 只调 C API。

### 3.2（历史）部署强制 VC143 CRT

曾在 `copy_release_dlls.cmake` 中 `-DFORCE_VC143=1`，避免旁路 VC142 CRT 与 14.44 引擎冲突。  
**现行**：与 v142 引擎一致，按当前工具集部署 CRT，不再强制 VC143。

### 3.3 现行部署清单

| 文件 | 说明 |
|------|------|
| `mock_algo_service.exe` | 直接链接 PCP（v142） |
| `PointCloudProcessor.dll` | **须为 v142 / linker 14.29** |
| 第三方 OpenCV/PCL/VTK 等 | 与 ReconDLL third_party 一致 |
| `config.json` | 点云算法配置 |
| ~~`pcp_c_api.dll`~~ | **已删除，客户包中应移除** |

---

## 4. 根本治理（推荐中长期）

桥接是兼容「引擎已是 14.44、主工程仍 VS2019」的工程补丁。要从根上去掉这类问题：

**调用方与 `PointCloudProcessor` 使用同一 MSVC 工具集重编。**

| 方案 | 做法 |
|------|------|
| A（保 VS2019） | 用 **v142** 重编 `PointCloudProcessor`（及同边界的 C++ 导出依赖），算法进程可直接链 PCP，桥接可退役 |
| B（全面升级） | `mock_algo_service`（及必要依赖）升到 **v143**，与现有 PCP 对齐 |

不要长期依赖：

- 跨工具集直接传递 C++ 对象；
- 客户机「只装 VC++ 红包」而不改部署包；
- exe 旁混放与引擎工具集不一致的 CRT。

---

## 5. 排查速查

| 检查项 | 方法 |
|--------|------|
| DLL 链接器版本 | `dumpbin /headers xxx.dll \| findstr "linker version"` |
| 是否仍直接链 PCP | 算法进程依赖中应有 `pcp_c_api.dll`，且创建路径走 `pcp_create` |
| 旁路 CRT 是否 VC143 | 与 `VC\Redist\MSVC\14.44.*\x64\Microsoft.VC143.CRT` 比对哈希 |
| 复现探针 | 旁路仅放 `pcp_c_api.dll` + PCP + 调用 `pcp_create`；再分别放入 VC142 / VC143 CRT 对照 |

---

## 6. 相关代码（现行）

| 路径 | 作用 |
|------|------|
| `tools/mock_algo_service/algo_processor_create.*` | 直接构造/调用 `PointCloudProcessor` |
| `tools/mock_algo_service/PointCloudProcessor_api.h` | 精简头（避免 pcl_visualizer/VTK 静态初始化） |
| `tools/mock_algo_service/CMakeLists.txt` | 直接链接 PCP + 第三方 |
| `tools/mock_algo_service/cmake/copy_release_dlls.cmake` | 第三方 DLL + 常规 MSVC CRT |
| `cmake/deploy_msvc_runtime.cmake` | CRT 部署 |

---

## 7. 结论

- **历史原因**：引擎 14.44 与算法进程 14.29 工具集不一致。  
- **临时补丁（已移除）**：C ABI 桥 + 强制 VC143。  
- **现行**：引擎与进程均为 **v142**，直接调用；部署时保证 `PointCloudProcessor.dll` 为 14.29，并清除旧包中的 `pcp_c_api.dll`。

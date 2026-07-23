# 问题记录：真机算法引擎构造访问冲突（MSVC ABI / CRT）

> 日期：2026-07-24  
> 状态：已修复（C ABI 桥接 + 强制部署 VC143 CRT）  
> 影响模块：`alg_program` / `mock_algo_service` / `PointCloudProcessor.dll`

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

## 3. 解决方案（已落地）

### 3.1 C ABI 桥接 DLL（隔离工具集）

新增 `pcp_c_api`（**必须用 VS2022 / v143 编译**），与 `PointCloudProcessor.dll` 对齐：

- 路径：`tools/mock_algo_service/pcp_c_api/`
- 导出纯 C 接口：`pcp_create` / `pcp_destroy` / `pcp_load_depth_map` / `pcp_process` / …
- 深度图以原始缓冲 + `(rows, cols, type)` 传递，**不跨 DLL 传 `std::string` / `cv::Mat`**

`mock_algo_service`（可仍为 VS2019）仅链接 `pcp_c_api.lib`，通过 `algo_processor_create.cpp` 调用上述 C API。

构建：父工程 POST/自定义步骤使用生成器 `Visual Studio 17 2022` 单独编出 `pcp_c_api.dll`。

### 3.2 部署强制覆盖 VC143 CRT

`tools/mock_algo_service/cmake/copy_release_dlls.cmake` 末尾调用：

```text
deploy_msvc_runtime.cmake -DFORCE_VC143=1
```

将 VS2022 红包中的 CRT **覆盖**到 `mock_algo_service` 输出目录，避免旁路残留 VC142。

### 3.3 客户机部署清单

替换整份 `alg_program`（或至少保证下列文件为新构建产物）：

| 文件 | 说明 |
|------|------|
| `mock_algo_service.exe` | 走 C API 的新算法进程 |
| `pcp_c_api.dll` | **新增，必须** |
| `PointCloudProcessor.dll` | 仍为现有 14.44 引擎 |
| `msvcp140.dll` 等 CRT | 须为 **VC143**，勿混用旧旁路 CRT |
| 其余 OpenCV/PCL/VTK 等依赖 | 与现有 Release 部署一致 |
| `config.json` | 点云算法配置 |

成功日志应出现类似：

```text
已加载算法引擎: ...\config.json
深度图 ... 
process 开始 ...
```

而不是构造期「原生异常/访问冲突」。

本地构建产物目录示例：

```text
visual_system/build/tools/mock_algo_service/Release/
```

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

## 6. 相关代码与脚本

| 路径 | 作用 |
|------|------|
| `tools/mock_algo_service/pcp_c_api/` | C ABI 桥接源码与独立 CMake |
| `tools/mock_algo_service/algo_processor_create.*` | 算法进程侧 C API 封装 |
| `tools/mock_algo_service/CMakeLists.txt` | 触发 VS2022 编桥接并部署 |
| `tools/mock_algo_service/cmake/copy_release_dlls.cmake` | 第三方 DLL + 强制 VC143 |
| `cmake/deploy_msvc_runtime.cmake` | CRT 部署（支持 `FORCE_VC143`） |

---

## 7. 结论

- **原因**：`PointCloudProcessor`（14.44）与算法进程（14.29）工具集不一致，跨 DLL 使用 C++ ABI；客户包旁路 VC142 CRT 使问题必现。  
- **已采用方案**：`pcp_c_api`（v143）纯 C 桥接 + 部署强制 VC143 CRT。  
- **中长期**：算法 DLL 与调用进程同工具集重编；安装 VC++ 运行库仅作缺 DLL 保底，不能替代本次修复。

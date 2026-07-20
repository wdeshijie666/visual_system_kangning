/**
 * @file main.cpp
 * @brief Visual System 主程序入口。
 *
 * 启动阶段通路（详见 docs/框架流程通路.md §三）：
 *   单实例 → 加载配置 → 后台清理/算法进程 → 装配 PLC/算法/相机 → 显示 UI
 * 产线周期由 MainWindow 工具栏「启动」后 SequenceEngine::Start 开始，不在此自动启动。
 */
#include <QApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QSystemSemaphore>

#include "main_window.h"
#include "algo_process_manager.h"
#include "visual/app_context.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"
#include "visual/i_mes_reporter.h"
#include "visual/rvc_camera_adapter.h"
#include "visual/sequence_engine.h"
#include "visual/shm_algo_service.h"
#include "visual/simulation_profile.h"
#include "visual/vision_plc_adapter.h"

#ifdef _WIN32
#include <Windows.h>
#include <dbghelp.h>
#pragma comment(lib, "DbgHelp.lib")

LONG WINAPI VsUnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  WCHAR path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  WCHAR* slash = wcsrchr(path, L'\\');
  if (slash) {
    *(slash + 1) = L'\0';
  }
  wcscat_s(path, L"dumps");
  CreateDirectoryW(path, nullptr);
  WCHAR dump[MAX_PATH];
  swprintf_s(dump, L"%sdumps\\VisualSystem_%04d%02d%02d.dmp", path, st.wYear, st.wMonth, st.wDay);
  HANDLE h = CreateFileW(dump, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = pExceptionPointers;
    info.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpNormal, &info, nullptr, nullptr);
    CloseHandle(h);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool EnsureSingleInstance() {
  const QString key = "VisualSystem_SharedMemory";
  QSystemSemaphore sem(key + "_sem", 1);
  sem.acquire();
  QSharedMemory mem(key);
  if (mem.attach() || !mem.create(1)) {
    sem.release();
    QLocalSocket sock;
    sock.connectToServer("VisualSystemApp");
    if (sock.waitForConnected(500)) {
      sock.write("ACTIVATE");
      sock.waitForBytesWritten(500);
    }
    return false;
  }
  sem.release();
  return true;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  SetUnhandledExceptionFilter(VsUnhandledExceptionFilter);
#endif

  QApplication app(argc, argv);
  app.setStyle(QStringLiteral("Fusion"));
  // 配置/数据路径均相对程序目录，避免从 IDE 启动时 CWD 不同读到错误 setting.json
  QDir::setCurrent(QCoreApplication::applicationDirPath());
  qRegisterMetaType<visual::StationId>("visual::StationId");
  qRegisterMetaType<visual::CycleResultEvent>("visual::CycleResultEvent");

  if (!EnsureSingleInstance()) {
    return 0;
  }
  QLocalServer server;
  server.listen("VisualSystemApp");

  // --- 阶段 0.2：加载 setting.json / devices.json ---
  visual::AppContext::Instance().Load();

  const auto& settings = visual::AppContext::Instance().Settings();
  const bool simulation_mode = visual::IsSimulationMode(settings);
  visual::SetSimulationProfile(simulation_mode, settings.simulation.algo_result);
  visual::EventBus::Instance().NotifyLog(
      QStringLiteral("runMode=%1").arg(QString::fromUtf8(visual::RunModeToString(settings.run_mode))));
  visual::EventBus::Instance().NotifyLog(
      QStringLiteral("dataStub saveDepth=%1 savePointcloud=%2 dataRoot=%3")
          .arg(settings.stub_save_depth)
          .arg(settings.stub_save_pointcloud)
          .arg(QString::fromStdString(visual::ResolveDataRoot(settings.data_path))));

  // --- 阶段 0.4：后台常驻 — 历史存根 7 天清理（与单次周期无关）---
  visual::DataStubRetentionCleaner data_stub_cleaner;
  data_stub_cleaner.Start(visual::AppContext::Instance().Settings().data_path);
  QObject::connect(&app, &QApplication::aboutToQuit, [&data_stub_cleaner]() { data_stub_cleaner.Stop(); });

  // --- 阶段 0.5：创建 PLC / 算法 / MES 依赖（注入 SequenceEngine）---
  auto plc = std::make_shared<visual::VisionPlcAdapter>();
  std::shared_ptr<visual::IAlgoService> algo;
  if (settings.use_shm_algo) {
    // 默认：独立算法进程 + SHM v2（setting.json → algo.shmName）
    algo = std::make_shared<visual::ShmAlgoService>(settings.algo_shm_name);
  } else {
    // 调试：进程内 Mock，不经 SHM
    algo = std::make_shared<visual::MockAlgoService>();
  }
  auto mes = std::make_shared<visual::NullMesReporter>();

  visual::StubCameraOptions stub_options;
  stub_options.image_width = settings.simulation.image_width;
  stub_options.image_height = settings.simulation.image_height;
  stub_options.solid_black = true;

  // --- 阶段 0.6：拉起 mock_algo_service.exe，同步 alg_program/algo_config.json ---
  std::unique_ptr<AlgoProcessManager> algo_process_manager;
  if (settings.use_shm_algo) {
    algo_process_manager = std::make_unique<AlgoProcessManager>(&app);
    algo_process_manager->Configure(QString::fromStdString(settings.algo_program_dir),
                                    QString::fromStdString(settings.algo_program_exe));
    algo_process_manager->SetRunModeSync(simulation_mode, settings.simulation.image_width,
                                         settings.simulation.image_height,
                                         settings.simulation.algo_result);
    if (!algo_process_manager->Start()) {
      visual::EventBus::Instance().NotifyLog(QStringLiteral("算法进程管理器启动失败"));
    }
  }

  // --- 阶段 0.5 续：装配 SequenceEngine（Worker 需 UI「启动」后才运行）---
  auto engine = std::make_shared<visual::SequenceEngine>();
  engine->SetPlcClient(plc);
  engine->SetAlgoService(algo);
  engine->SetMesReporter(mes);

  // --- 阶段 1：设备初始化 — 相机 Connect 并注册到引擎 ---
  for (const auto& kv : visual::AppContext::Instance().Devices()) {
    auto cam = visual::CreateRvcCamera(kv.second.id, kv.second.serial, simulation_mode, stub_options);
    cam->Connect();
    engine->RegisterCamera(kv.second.id, cam);
    visual::EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.second.id), cam->IsConnected());
  }

  // 退出顺序：停编排 → 断相机（Close/Destroy/SystemShutdown）→ 停算法进程
  QObject::connect(&app, &QApplication::aboutToQuit, [&engine, &algo_process_manager]() {
    if (engine) {
      engine->Stop();
      engine->DisconnectAllCameras();
    }
    if (algo_process_manager) {
      algo_process_manager->Stop();
    }
  });

  // --- 阶段 0.7：显示 UI；产线轮询见 MainWindow::OnStartEngine ---
  MainWindow window(engine, simulation_mode);
  window.resize(1280, 800);
  window.InitUi();
  window.show();

  return app.exec();
}

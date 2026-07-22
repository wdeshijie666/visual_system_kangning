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
#include "visual/alarm_service.h"
#include "visual/app_context.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"
#include "visual/file_mes_reporter.h"
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
  qRegisterMetaType<visual::AlarmRecord>("visual::AlarmRecord");
  // 在主线程先构造，保证后续跨线程 Notify/Raise 能排队回 UI 线程
  (void)visual::EventBus::Instance();
  (void)visual::AlarmService::Instance();

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
      QStringLiteral("运行模式=%1  R05=%2  R09=%3")
          .arg(QString::fromUtf8(visual::RunModeToString(settings.run_mode)))
          .arg(settings.station_r05.enabled ? QStringLiteral("启用") : QStringLiteral("停用"))
          .arg(settings.station_r09.enabled ? QStringLiteral("启用") : QStringLiteral("停用")));
  if (simulation_mode) {
    visual::EventBus::Instance().NotifyLog(QStringLiteral("当前为仿真模式"));
  } else {
    visual::EventBus::Instance().NotifyLog(QStringLiteral("当前为实机模式"));
  }

  // 工位相机配置交叉校验
  for (const auto& id : settings.station_r05.camera_ids) {
    if (visual::AppContext::Instance().Devices().find(id) ==
        visual::AppContext::Instance().Devices().end()) {
      visual::EventBus::Instance().NotifyLog(
          QStringLiteral("配置警告: R05 相机 %1 未在设备列表中")
              .arg(QString::fromStdString(id)));
    }
  }
  for (const auto& id : settings.station_r09.camera_ids) {
    if (visual::AppContext::Instance().Devices().find(id) ==
        visual::AppContext::Instance().Devices().end()) {
      visual::EventBus::Instance().NotifyLog(
          QStringLiteral("配置警告: R09 相机 %1 未在设备列表中")
              .arg(QString::fromStdString(id)));
    }
  }

  visual::EventBus::Instance().NotifyLog(
      QStringLiteral("存图 深度=%1 点云=%2 保留=%3天")
          .arg(settings.stub_save_depth ? QStringLiteral("开") : QStringLiteral("关"))
          .arg(settings.stub_save_pointcloud ? QStringLiteral("开") : QStringLiteral("关"))
          .arg(settings.data_retention_days));

  // --- 阶段 0.4：后台常驻 — 历史存根清理 ---
  visual::DataStubRetentionCleaner data_stub_cleaner;
  data_stub_cleaner.Start(visual::AppContext::Instance().Settings().data_path,
                          visual::AppContext::Instance().Settings().data_retention_days);
  QObject::connect(&app, &QApplication::aboutToQuit, [&data_stub_cleaner]() { data_stub_cleaner.Stop(); });

  // --- 阶段 0.5：创建 PLC / 算法 / MES 依赖（注入 SequenceEngine）---
  auto plc = std::make_shared<visual::VisionPlcAdapter>();
  std::shared_ptr<visual::ShmAlgoServicePool> algo_pool;
  std::shared_ptr<visual::IAlgoService> algo;
  if (settings.use_shm_algo) {
    // 双工位：各映射一块 SHM，按 station 选择通道
    algo_pool = std::make_shared<visual::ShmAlgoServicePool>();
    algo_pool->Configure(visual::shm::ShmChannelId::kR05, settings.algo_channel_r05.shm_name,
                         settings.algo_channel_r05.mutex_name);
    algo_pool->Configure(visual::shm::ShmChannelId::kR09, settings.algo_channel_r09.shm_name,
                         settings.algo_channel_r09.mutex_name);
    visual::EventBus::Instance().NotifyLog(QStringLiteral("算法共享内存通道已配置"));
  } else {
    algo = std::make_shared<visual::MockAlgoService>();
  }
  auto mes = std::make_shared<visual::FileMesReporter>();

  visual::StubCameraOptions stub_options;
  stub_options.image_width = settings.simulation.image_width;
  stub_options.image_height = settings.simulation.image_height;
  stub_options.solid_black = true;

  // --- 阶段 0.6：拉起 mock_algo_service.exe ---
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

  auto engine = std::make_shared<visual::SequenceEngine>();
  engine->SetPlcClient(plc);
  if (algo_pool) {
    engine->SetAlgoPool(algo_pool);
  } else {
    engine->SetAlgoService(algo);
  }
  engine->SetMesReporter(mes);

  for (const auto& kv : visual::AppContext::Instance().Devices()) {
    const bool use_stub_serial =
        kv.second.serial.size() >= 5 &&
        (kv.second.serial.compare(0, 5, "STUB_") == 0 || kv.second.serial.compare(0, 5, "stub_") == 0);
    auto cam = visual::CreateRvcCamera(kv.second.id, kv.second.serial, simulation_mode, stub_options);
    const bool ok = cam->Connect();
    engine->RegisterCamera(kv.second.id, cam);
    visual::EventBus::Instance().NotifyCameraStatus(QString::fromStdString(kv.second.id), ok);
    visual::EventBus::Instance().NotifyLog(
        QStringLiteral("相机连接 %1 序列号=%2 %3%4")
            .arg(QString::fromStdString(kv.second.id))
            .arg(QString::fromStdString(kv.second.serial))
            .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"))
            .arg(use_stub_serial || simulation_mode ? QStringLiteral("（仿真）") : QString()));
  }

  QObject::connect(&app, &QApplication::aboutToQuit, [&engine, &algo_process_manager]() {
    if (engine) {
      engine->Stop();
      engine->DisconnectAllCameras();
    }
    if (algo_process_manager) {
      algo_process_manager->Stop();
    }
  });

  MainWindow window(engine, simulation_mode);
  window.resize(1280, 800);
  window.InitUi();
  window.show();

  // 二次启动时把已有窗口前置
  QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
    QLocalSocket* sock = server.nextPendingConnection();
    if (sock != nullptr) {
      sock->deleteLater();
    }
    window.show();
    window.raise();
    window.activateWindow();
  });

  return app.exec();
}

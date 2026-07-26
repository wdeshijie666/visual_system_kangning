cmake_minimum_required(VERSION 3.20)
project(visual_system_app LANGUAGES CXX)

set(_vs_app_sources
  src/main.cpp
  src/main_window.cpp
  src/algo_process_manager.cpp
  src/startup_splash.cpp
  src/station_result_widget.cpp
  src/device_status_widget.cpp
  src/camera_manager_widget.cpp
  src/camera_recipe_paths.cpp
  src/viewport_widget.cpp
  src/offline_test_widget.cpp
)

set(_vs_app_headers
  include/main_window.h
  include/algo_process_manager.h
  include/startup_splash.h
  include/station_result_widget.h
  include/device_status_widget.h
  include/camera_manager_widget.h
  include/camera_recipe_paths.h
  include/viewport_widget.h
  include/offline_test_widget.h
)

if(WIN32)
  add_executable(VisualSystem WIN32 ${_vs_app_sources} ${_vs_app_headers})
else()
  add_executable(VisualSystem ${_vs_app_sources} ${_vs_app_headers})
endif()

target_include_directories(VisualSystem PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/app_core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/device_abstraction/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_plc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_rvc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_algo_shm/include
)

target_link_libraries(VisualSystem
  PRIVATE
    VisualSystem::app_core
    VisualSystem::adapter_plc
    VisualSystem::adapter_rvc
    VisualSystem::adapter_algo_shm
    Qt5::Widgets
    Qt5::Gui
    Qt5::Network
    DbgHelp
)

qt5_add_resources(VS_RESOURCES ../../resources/resources.qrc)
target_sources(VisualSystem PRIVATE ${VS_RESOURCES})

if(MSVC)
  target_compile_options(VisualSystem PRIVATE /utf-8)
endif()

vs_copy_runtime_dlls(VisualSystem)

add_custom_command(TARGET VisualSystem POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../config"
    "$<TARGET_FILE_DIR:VisualSystem>/config"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:VisualSystem>/camera_parmas"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:VisualSystem>/alg_program"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE_DIR:mock_algo_service>/mock_algo_service.exe"
    "$<TARGET_FILE_DIR:VisualSystem>/alg_program/mock_algo_service.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/mock_algo_service/algo_config.json"
    "$<TARGET_FILE_DIR:VisualSystem>/alg_program/algo_config.json"
  # 将算法进程旁的 DLL / config.json 一并同步到 alg_program（真实 PointCloud 依赖）
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "$<TARGET_FILE_DIR:mock_algo_service>"
    "$<TARGET_FILE_DIR:VisualSystem>/alg_program"
  VERBATIM
)

add_dependencies(VisualSystem mock_algo_service)

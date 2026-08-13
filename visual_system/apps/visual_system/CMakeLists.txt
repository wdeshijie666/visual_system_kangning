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
  src/repeatability_test_dialog.cpp
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
  include/repeatability_test_dialog.h
)

if(WIN32)
  add_executable(VisualSystem WIN32 ${_vs_app_sources} ${_vs_app_headers})
else()
  add_executable(VisualSystem ${_vs_app_sources} ${_vs_app_headers})
endif()

# CMake 目标名仍为 VisualSystem；产物文件名为 SmartGuide.exe
set_target_properties(VisualSystem PROPERTIES OUTPUT_NAME SmartGuide)

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
    Qt5::Svg
    DbgHelp
)

qt5_add_resources(VS_RESOURCES ../../resources/resources.qrc)
qt5_add_resources(VS_APP_ICON_RESOURCES resources/app_icons.qrc)
target_sources(VisualSystem PRIVATE ${VS_RESOURCES} ${VS_APP_ICON_RESOURCES})

if(WIN32)
  # 嵌入 exe 图标：ico 与生成的 .rc 同目录，避免 RC 对绝对/中文路径不友好
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.ico"
    "${CMAKE_CURRENT_BINARY_DIR}/app_icon.ico"
    COPYONLY)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    COPYONLY)
  target_sources(VisualSystem PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

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
  # 同步算法运行库；已存在的 algo_config.json 保留现场修改
  COMMAND ${CMAKE_COMMAND}
    "-DMOCK_DIR=$<TARGET_FILE_DIR:mock_algo_service>"
    "-DDST_DIR=$<TARGET_FILE_DIR:VisualSystem>/alg_program"
    "-DDEFAULT_ALGO_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/../../tools/mock_algo_service/algo_config.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sync_alg_program.cmake"
  VERBATIM
)

add_dependencies(VisualSystem mock_algo_service)

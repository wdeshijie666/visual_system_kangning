add_library(vs_app_core
  include/visual/event_bus.h
  include/visual/data_recorder.h
  include/visual/alarm_service.h
  include/visual/cycle_job_queue.h
  include/visual/fault_breaker.h
  include/visual/rotating_file_log.h
  include/visual/file_mes_reporter.h
  src/app_context.cpp
  src/data_recorder.cpp
  src/event_bus.cpp
  src/sequence_engine.cpp
  src/alarm_service.cpp
  src/rotating_file_log.cpp
  src/file_mes_reporter.cpp
)
add_library(VisualSystem::app_core ALIAS vs_app_core)

target_include_directories(vs_app_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../device_abstraction/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../adapter_algo_shm/include>
  PRIVATE
    ${VS_NLOHMANN_INCLUDE}
)

target_link_libraries(vs_app_core
  PUBLIC
    VisualSystem::device_abstraction
    VisualSystem::adapter_algo_shm
    Qt5::Core
)

target_compile_features(vs_app_core PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vs_app_core PRIVATE /utf-8)
endif()

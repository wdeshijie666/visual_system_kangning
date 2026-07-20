add_library(vs_app_core
  include/visual/event_bus.h
  include/visual/data_recorder.h
  src/app_context.cpp
  src/data_recorder.cpp
  src/event_bus.cpp
  src/sequence_engine.cpp
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

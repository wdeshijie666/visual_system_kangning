add_library(vs_adapter_plc src/vision_plc_adapter.cpp)
add_library(VisualSystem::adapter_plc ALIAS vs_adapter_plc)

target_include_directories(vs_adapter_plc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../device_abstraction/include>
)

target_link_libraries(vs_adapter_plc
  PUBLIC
    VisualSystem::device_abstraction
    VisionPlc::vision_plc_driver
)

target_compile_features(vs_adapter_plc PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vs_adapter_plc PRIVATE /utf-8)
endif()

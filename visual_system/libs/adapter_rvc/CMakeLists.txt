add_library(vs_adapter_rvc src/rvc_camera_adapter.cpp)
add_library(VisualSystem::adapter_rvc ALIAS vs_adapter_rvc)

target_include_directories(vs_adapter_rvc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../device_abstraction/include>
)

if(VISUAL_HAS_RVC_SDK)
  target_compile_definitions(vs_adapter_rvc PRIVATE VISUAL_HAS_RVC_SDK=1)
  target_include_directories(vs_adapter_rvc PRIVATE "${RVC_ROOT}/include")
  if(VS_RVC_LIB)
    target_link_libraries(vs_adapter_rvc PRIVATE "${VS_RVC_LIB}")
  endif()
endif()

target_link_libraries(vs_adapter_rvc PUBLIC VisualSystem::device_abstraction)
target_compile_features(vs_adapter_rvc PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vs_adapter_rvc PRIVATE /utf-8)
endif()

add_library(vs_device_abstraction INTERFACE)
add_library(VisualSystem::device_abstraction ALIAS vs_device_abstraction)

target_include_directories(vs_device_abstraction INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(vs_device_abstraction INTERFACE cxx_std_17)

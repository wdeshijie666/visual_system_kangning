add_library(vs_adapter_algo_shm
  src/algo_shm_codec.cpp
  src/shm_algo_service.cpp
)
add_library(VisualSystem::adapter_algo_shm ALIAS vs_adapter_algo_shm)

target_include_directories(vs_adapter_algo_shm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../device_abstraction/include>
)

target_link_libraries(vs_adapter_algo_shm PUBLIC VisualSystem::device_abstraction)
target_compile_features(vs_adapter_algo_shm PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vs_adapter_algo_shm PRIVATE /utf-8)
endif()

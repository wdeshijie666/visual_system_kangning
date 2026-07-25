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

# Stub 读 sim_test.tiff：复用 ReconDLL 已统一的 OpenCV 4.12（仅仿真路径）
set(VS_RECON_THIRD_PARTY_ROOT "G:/ReconDLL/third_party" CACHE PATH "OpenCV for stub depth TIFF")
set(VISUAL_STUB_HAS_OPENCV OFF)
if(EXISTS "${VS_RECON_THIRD_PARTY_ROOT}/include/opencv4/opencv2/imgcodecs.hpp"
   AND EXISTS "${VS_RECON_THIRD_PARTY_ROOT}/lib/opencv_core4.lib")
  set(VISUAL_STUB_HAS_OPENCV ON)
  target_compile_definitions(vs_adapter_rvc PRIVATE VISUAL_STUB_HAS_OPENCV=1)
  target_include_directories(vs_adapter_rvc PRIVATE
    "${VS_RECON_THIRD_PARTY_ROOT}/include"
    "${VS_RECON_THIRD_PARTY_ROOT}/include/opencv4")
  target_link_libraries(vs_adapter_rvc PRIVATE
    "${VS_RECON_THIRD_PARTY_ROOT}/lib/opencv_core4.lib"
    "${VS_RECON_THIRD_PARTY_ROOT}/lib/opencv_imgproc4.lib"
    "${VS_RECON_THIRD_PARTY_ROOT}/lib/opencv_imgcodecs4.lib")
  message(STATUS "Stub depth TIFF via OpenCV: ${VS_RECON_THIRD_PARTY_ROOT}")
else()
  message(WARNING "OpenCV not found under ${VS_RECON_THIRD_PARTY_ROOT}; Stub will not load sim_test.tiff")
endif()

target_link_libraries(vs_adapter_rvc PUBLIC VisualSystem::device_abstraction)
target_compile_features(vs_adapter_rvc PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vs_adapter_rvc PRIVATE /utf-8)
endif()

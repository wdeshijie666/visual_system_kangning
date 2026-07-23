include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ThirdPartyPaths.cmake)

# ---------------------------------------------------------------------------
# 真实点云算法：经 pcp_c_api（VS2022）桥接 PointCloudProcessor（避免 MSVC 14.29↔14.44 ABI 崩溃）
# ---------------------------------------------------------------------------
set(VS_POINTCLOUD_ROOT "G:/ReconDLL/point_cloud" CACHE PATH "PointCloudProcessor root (header + Release/Debug)")
set(VS_RECON_THIRD_PARTY_ROOT "G:/ReconDLL/third_party" CACHE PATH "ReconDLL packaged OpenCV/PCL/VTK root")

set(VS_HAS_POINTCLOUD_ALGO OFF)
if(EXISTS "${VS_POINTCLOUD_ROOT}/PointCloudProcessor.h"
   AND EXISTS "${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.lib"
   AND EXISTS "${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")
  set(VS_HAS_POINTCLOUD_ALGO ON)
  message(STATUS "PointCloud algo enabled via pcp_c_api bridge: ${VS_POINTCLOUD_ROOT}")
else()
  message(WARNING "PointCloud algo NOT found under ${VS_POINTCLOUD_ROOT}; mock_algo_service will use mock logs only")
endif()

set(_mock_algo_sources
  main.cpp
  algo_config.cpp
  algo_input_converter.cpp
  algo_online_service.cpp
  algo_offline_replay.cpp
)
if(VS_HAS_POINTCLOUD_ALGO)
  list(APPEND _mock_algo_sources algo_pointcloud_runner.cpp algo_processor_create.cpp)
endif()

add_executable(mock_algo_service ${_mock_algo_sources})

target_include_directories(mock_algo_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_algo_shm/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/device_abstraction/include
  ${VS_NLOHMANN_INCLUDE}
)

target_sources(mock_algo_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_algo_shm/src/algo_shm_codec.cpp
)

if(MSVC)
  target_compile_options(mock_algo_service PRIVATE /utf-8)
endif()

# 与 OpenCV / 桥接 DLL 一致：动态 CRT
if(MSVC)
  set_property(TARGET mock_algo_service PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(VS_HAS_POINTCLOUD_ALGO)
  include("${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")

  # ---- 用 VS2022 单独构建 C ABI 桥接 DLL（与 PointCloudProcessor linker 14.44 对齐）----
  set(_pcp_bridge_src "${CMAKE_CURRENT_SOURCE_DIR}/pcp_c_api")
  set(_pcp_bridge_bin "${CMAKE_CURRENT_BINARY_DIR}/pcp_c_api_vs2022")
  # pcp_c_api/CMakeLists 将 Release/Debug 输出到 bin/、lib/（无配置子目录）
  set(_pcp_bridge_dll "${_pcp_bridge_bin}/bin/pcp_c_api.dll")
  set(_pcp_bridge_lib "${_pcp_bridge_bin}/lib/pcp_c_api.lib")

  add_custom_command(
    OUTPUT "${_pcp_bridge_bin}/.stamp" "${_pcp_bridge_lib}" "${_pcp_bridge_dll}"
    COMMAND ${CMAKE_COMMAND}
      -G "Visual Studio 17 2022" -A x64
      -S "${_pcp_bridge_src}"
      -B "${_pcp_bridge_bin}"
      -DVS_POINTCLOUD_ROOT=${VS_POINTCLOUD_ROOT}
      -DVS_RECON_THIRD_PARTY_ROOT=${VS_RECON_THIRD_PARTY_ROOT}
      -DVS_NLOHMANN_INCLUDE=${VS_NLOHMANN_INCLUDE}
    COMMAND ${CMAKE_COMMAND} --build "${_pcp_bridge_bin}" --config Release
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_pcp_bridge_bin}/bin/pcp_c_api.dll" "${_pcp_bridge_dll}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_pcp_bridge_bin}/lib/pcp_c_api.lib" "${_pcp_bridge_lib}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_pcp_bridge_bin}/.stamp"
    DEPENDS
      "${_pcp_bridge_src}/pcp_c_api.cpp"
      "${_pcp_bridge_src}/pcp_c_api.h"
      "${_pcp_bridge_src}/CMakeLists.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/PointCloudProcessor_api.h"
    COMMENT "Building pcp_c_api.dll with VS2022 (ABI match PointCloudProcessor)"
    VERBATIM
  )
  add_custom_target(pcp_c_api_build DEPENDS "${_pcp_bridge_bin}/.stamp")

  add_library(pcp_c_api SHARED IMPORTED GLOBAL)
  set_target_properties(pcp_c_api PROPERTIES
    IMPORTED_LOCATION "${_pcp_bridge_dll}"
    IMPORTED_IMPLIB "${_pcp_bridge_lib}"
  )
  add_dependencies(pcp_c_api pcp_c_api_build)
  add_dependencies(mock_algo_service pcp_c_api_build)

  set(OPENCV_LIBS
    "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/opencv_core480.lib>"
    "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/opencv_core480d.lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/opencv_imgproc480.lib>"
    "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/opencv_imgproc480d.lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/opencv_imgcodecs480.lib>"
    "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/opencv_imgcodecs480d.lib>"
  )

  target_include_directories(mock_algo_service PRIVATE
    "${THIRD_PARTY_INCLUDE_DIR}"
    "${THIRD_PARTY_INCLUDE_DIR}/opencv4"
  )

  target_compile_definitions(mock_algo_service PRIVATE
    VS_HAS_POINTCLOUD_ALGO=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
  )

  target_link_libraries(mock_algo_service PRIVATE
    pcp_c_api
    ${OPENCV_LIBS}
  )

  # 运行时：桥接 DLL + PointCloudProcessor + 第三方 + config
  add_custom_command(TARGET mock_algo_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_pcp_bridge_dll}"
      "$<TARGET_FILE_DIR:mock_algo_service>/pcp_c_api.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.dll"
      "$<TARGET_FILE_DIR:mock_algo_service>/PointCloudProcessor.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/point_cloud_config.json"
      "$<TARGET_FILE_DIR:mock_algo_service>/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/algo_config.json"
      "$<TARGET_FILE_DIR:mock_algo_service>/algo_config.json"
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${THIRD_PARTY_BIN_DIR}
      -DSRC_RELEASE=${CMAKE_CURRENT_SOURCE_DIR}/runtime_release_overrides
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_release_dlls.cmake"
    COMMENT "Deploy pcp_c_api + PointCloudProcessor + third-party + MSVC DLLs"
    VERBATIM
  )
else()
  add_custom_command(TARGET mock_algo_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/algo_config.json"
      "$<TARGET_FILE_DIR:mock_algo_service>/algo_config.json"
    COMMAND ${CMAKE_COMMAND}
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/deploy_msvc_runtime.cmake"
    COMMENT "Deploy MSVC runtime DLLs beside mock_algo_service"
    VERBATIM
  )
endif()

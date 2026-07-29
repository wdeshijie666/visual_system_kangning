include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ThirdPartyPaths.cmake)

# ---------------------------------------------------------------------------
# 真实点云算法：直接链接 PointCloudProcessor（与 mock_algo_service 同为 MSVC v142）
# ---------------------------------------------------------------------------
set(VS_POINTCLOUD_ROOT "G:/ReconDLL/point_cloud" CACHE PATH "PointCloudProcessor root (header + Release/Debug)")
set(VS_RECON_THIRD_PARTY_ROOT "G:/ReconDLL/third_party" CACHE PATH "ReconDLL packaged OpenCV/PCL/VTK root")

set(VS_HAS_POINTCLOUD_ALGO OFF)
if(EXISTS "${VS_POINTCLOUD_ROOT}/PointCloudProcessor.h"
   AND EXISTS "${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.lib"
   AND EXISTS "${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")
  set(VS_HAS_POINTCLOUD_ALGO ON)
  message(STATUS "PointCloud algo enabled: ${VS_POINTCLOUD_ROOT}")
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
  # 捕获第三方引擎构造/调用期 SEH（ACCESS_VIOLATION），避免整进程退出
  set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/algo_processor_create.cpp"
    PROPERTIES COMPILE_OPTIONS "/EHa")
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

if(MSVC)
  set_property(TARGET mock_algo_service PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(VS_HAS_POINTCLOUD_ALGO)
  include("${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")
  link_directories("${THIRD_PARTY_LIB_DIR}")

  function(tp_lib out_var rel dbg)
    set(_libs ${${out_var}})
    list(APPEND _libs
      "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/${rel}>"
      "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/${dbg}>"
    )
    set(${out_var} "${_libs}" PARENT_SCOPE)
  endfunction()

  set(OPENCV_LIBS "")
  # OpenCV 4.12（与 PointCloudProcessor 一致，导入库后缀 4）
  foreach(_m core imgproc imgcodecs calib3d highgui features2d flann)
    tp_lib(OPENCV_LIBS "opencv_${_m}4.lib" "opencv_${_m}4d.lib")
  endforeach()

  set(PCL_LIBS "")
  foreach(_m common io io_ply kdtree octree search sample_consensus
              filters features segmentation surface visualization)
    tp_lib(PCL_LIBS "pcl_${_m}.lib" "pcl_${_m}d.lib")
  endforeach()

  file(GLOB _vtk_all "${THIRD_PARTY_LIB_DIR}/vtk*-9.3.lib")
  file(GLOB _vtk_dbg "${THIRD_PARTY_LIB_DIR}/vtk*-9.3d.lib")
  set(_vtk_rel ${_vtk_all})
  if(_vtk_dbg)
    list(REMOVE_ITEM _vtk_rel ${_vtk_dbg})
  endif()
  set(VTK_LIBS "")
  foreach(_lib IN LISTS _vtk_rel)
    list(APPEND VTK_LIBS "$<$<NOT:$<CONFIG:Debug>>:${_lib}>")
  endforeach()
  foreach(_lib IN LISTS _vtk_dbg)
    list(APPEND VTK_LIBS "$<$<CONFIG:Debug>:${_lib}>")
  endforeach()

  set(POINTCLOUD_LIB_PATH
    "$<$<NOT:$<CONFIG:Debug>>:${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.lib>"
    "$<$<CONFIG:Debug>:${VS_POINTCLOUD_ROOT}/Debug/PointCloudProcessor.lib>"
  )

  target_include_directories(mock_algo_service PRIVATE
    "${VS_POINTCLOUD_ROOT}"
    "${THIRD_PARTY_INCLUDE_DIR}"
    "${THIRD_PARTY_INCLUDE_DIR}/opencv4"
    "${THIRD_PARTY_INCLUDE_DIR}/eigen3"
    "${THIRD_PARTY_INCLUDE_DIR}/vtk-9.3"
  )

  target_compile_definitions(mock_algo_service PRIVATE
    VS_HAS_POINTCLOUD_ALGO=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
    BOOST_ALL_NO_LIB=0
  )

  target_link_libraries(mock_algo_service PRIVATE
    ${POINTCLOUD_LIB_PATH}
    ${OPENCV_LIBS}
    ${PCL_LIBS}
    ${VTK_LIBS}
  )

  if(MSVC)
    target_compile_options(mock_algo_service PRIVATE /bigobj)
  endif()

  add_custom_command(TARGET mock_algo_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f
      "$<TARGET_FILE_DIR:mock_algo_service>/pcp_c_api.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.dll"
      "$<TARGET_FILE_DIR:mock_algo_service>/PointCloudProcessor.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/point_cloud_config.json"
      "$<TARGET_FILE_DIR:mock_algo_service>/config.json"
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/algo_config.json
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>/algo_config.json
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/copy_algo_config_if_missing.cmake"
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${THIRD_PARTY_BIN_DIR}
      -DSRC_RELEASE=${CMAKE_CURRENT_SOURCE_DIR}/runtime_release_overrides
      -DWEBP_RELEASE_DIR=G:/ReconDLL/Decode/Release
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_release_dlls.cmake"
    COMMENT "Deploy PointCloudProcessor + third-party + MSVC DLLs beside mock_algo_service"
    VERBATIM
  )
else()
  add_custom_command(TARGET mock_algo_service POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/algo_config.json
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>/algo_config.json
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/copy_algo_config_if_missing.cmake"
    COMMAND ${CMAKE_COMMAND}
      -DDST=$<TARGET_FILE_DIR:mock_algo_service>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/deploy_msvc_runtime.cmake"
    COMMENT "Deploy MSVC runtime DLLs beside mock_algo_service"
    VERBATIM
  )
endif()

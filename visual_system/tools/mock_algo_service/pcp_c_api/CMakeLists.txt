cmake_minimum_required(VERSION 3.20)
project(pcp_c_api LANGUAGES CXX)

# 本目标必须用 VS2022 / toolset v143 构建，以匹配 PointCloudProcessor.dll (linker 14.44)

set(VS_POINTCLOUD_ROOT "G:/ReconDLL/point_cloud" CACHE PATH "PointCloudProcessor root")
set(VS_RECON_THIRD_PARTY_ROOT "G:/ReconDLL/third_party" CACHE PATH "ReconDLL third_party")
set(VS_NLOHMANN_INCLUDE "" CACHE PATH "nlohmann/json include dir")

if(NOT EXISTS "${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")
  message(FATAL_ERROR "third_party.cmake not found: ${VS_RECON_THIRD_PARTY_ROOT}")
endif()
include("${VS_RECON_THIRD_PARTY_ROOT}/third_party.cmake")

add_library(pcp_c_api SHARED pcp_c_api.cpp)
target_compile_definitions(pcp_c_api PRIVATE
  PCP_C_API_EXPORTS
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _USE_MATH_DEFINES
  _CRT_SECURE_NO_WARNINGS
)
target_include_directories(pcp_c_api PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  "${VS_POINTCLOUD_ROOT}"
  "${THIRD_PARTY_INCLUDE_DIR}"
  "${THIRD_PARTY_INCLUDE_DIR}/opencv4"
  "${THIRD_PARTY_INCLUDE_DIR}/eigen3"
  "${THIRD_PARTY_INCLUDE_DIR}/vtk-9.3"
)
if(VS_NLOHMANN_INCLUDE)
  target_include_directories(pcp_c_api PRIVATE "${VS_NLOHMANN_INCLUDE}")
endif()

set_target_properties(pcp_c_api PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_options(pcp_c_api PRIVATE /utf-8 /EHa /bigobj /std:c++17)
  set_property(TARGET pcp_c_api PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(POINTCLOUD_LIB
  "$<$<NOT:$<CONFIG:Debug>>:${VS_POINTCLOUD_ROOT}/Release/PointCloudProcessor.lib>"
  "$<$<CONFIG:Debug>:${VS_POINTCLOUD_ROOT}/Debug/PointCloudProcessor.lib>"
)

set(OPENCV_LIBS
  "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/opencv_core480.lib>"
  "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/opencv_core480d.lib>"
)

set(PCL_LIBS "")
foreach(_m common io io_ply kdtree octree search sample_consensus filters features segmentation surface visualization)
  list(APPEND PCL_LIBS
    "$<$<NOT:$<CONFIG:Debug>>:${THIRD_PARTY_LIB_DIR}/pcl_${_m}.lib>"
    "$<$<CONFIG:Debug>:${THIRD_PARTY_LIB_DIR}/pcl_${_m}d.lib>"
  )
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

target_link_libraries(pcp_c_api PRIVATE
  ${POINTCLOUD_LIB}
  ${OPENCV_LIBS}
  ${PCL_LIBS}
  ${VTK_LIBS}
)

# 便于宿主复制：同时输出到约定目录
set_target_properties(pcp_c_api PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
)

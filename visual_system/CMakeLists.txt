cmake_minimum_required(VERSION 3.20)
project(VisualSystem VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT THIRD_PARTY_LIBRARY_DIR)
  if(DEFINED ENV{THIRD_PARTY_LIBRARY_DIR} AND NOT "$ENV{THIRD_PARTY_LIBRARY_DIR}" STREQUAL "")
    set(THIRD_PARTY_LIBRARY_DIR "$ENV{THIRD_PARTY_LIBRARY_DIR}" CACHE PATH "" FORCE)
  else()
    set(THIRD_PARTY_LIBRARY_DIR "G:/3rdParty" CACHE PATH "" FORCE)
  endif()
endif()

set(RVC_ROOT "D:/Program Files/RVBUST/RVC/RVCSDK" CACHE PATH "RVC SDK root (include/lib/runtime)")
set(RVC_LIB_DIR "${RVC_ROOT}/lib" CACHE PATH "RVC.lib directory")
set(RVC_RUNTIME_DIR "${RVC_ROOT}/runtime" CACHE PATH "Directory containing RVC.dll")
set(Qt5_DIR "D:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5" CACHE PATH "")

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Network)

set(AB_PLC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../AB_PLC" CACHE PATH "AB_PLC root")
set(VISION_PLC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISION_PLC_BUILD_REAL_EXAMPLE OFF CACHE BOOL "" FORCE)
# 真机 AB PLC：配置时 -DVISION_PLC_ENABLE_LIBPLCTAG=ON（勿 FORCE，否则命令行无效）
set(VISION_PLC_ENABLE_LIBPLCTAG OFF CACHE BOOL "ON=libplctag 真机；OFF=MemoryTransport 仿真")
add_subdirectory(${AB_PLC_DIR} ${CMAKE_BINARY_DIR}/ab_plc_build)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdPartyPaths.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDlls.cmake)

add_subdirectory(libs/device_abstraction)
add_subdirectory(libs/adapter_algo_shm)
add_subdirectory(libs/adapter_plc)
add_subdirectory(libs/adapter_rvc)
add_subdirectory(libs/app_core)
add_subdirectory(apps/visual_system)
add_subdirectory(tools/mock_algo_service)

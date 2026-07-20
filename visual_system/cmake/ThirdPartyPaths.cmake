set(VS_NLOHMANN_INCLUDE "${THIRD_PARTY_LIBRARY_DIR}/nlohmann/single_include")
if(NOT EXISTS "${VS_NLOHMANN_INCLUDE}/nlohmann/json.hpp")
  set(VS_NLOHMANN_INCLUDE "${THIRD_PARTY_LIBRARY_DIR}/nlohmann/include")
endif()

set(VS_SPDLOG_INCLUDE "${THIRD_PARTY_LIBRARY_DIR}/spdlog/include")
set(VS_SPDLOG_LIB "${THIRD_PARTY_LIBRARY_DIR}/spdlog/lib/spdlog.lib")

# RVC SDK（如本 RVC X 系列相机）
#   SDK 根目录：D:/Program Files/RVBUST/RVC/RVCSDK
#   导入库目录：${RVC_ROOT}/lib  → RVC.lib
if(NOT RVC_LIB_DIR)
  set(RVC_LIB_DIR "${RVC_ROOT}/lib" CACHE PATH "RVC SDK import library directory (RVC.lib)")
endif()
if(NOT RVC_RUNTIME_DIR)
  set(RVC_RUNTIME_DIR "${RVC_ROOT}/runtime" CACHE PATH "RVC SDK runtime DLL directory")
endif()

set(VS_RVC_LIB "")
if(EXISTS "${RVC_LIB_DIR}/RVC.lib")
  set(VS_RVC_LIB "${RVC_LIB_DIR}/RVC.lib")
elseif(EXISTS "${RVC_ROOT}/lib/RVC.lib")
  set(VS_RVC_LIB "${RVC_ROOT}/lib/RVC.lib")
elseif(EXISTS "${RVC_ROOT}/lib/x64/RVC.lib")
  set(VS_RVC_LIB "${RVC_ROOT}/lib/x64/RVC.lib")
endif()

if(EXISTS "${RVC_ROOT}/include/RVC/RVC.h" AND VS_RVC_LIB)
  set(VISUAL_HAS_RVC_SDK ON)
  message(STATUS "RVC SDK enabled: root=${RVC_ROOT}, lib=${VS_RVC_LIB}")
else()
  set(VISUAL_HAS_RVC_SDK OFF)
  if(EXISTS "${RVC_ROOT}/include/RVC/RVC.h")
    message(WARNING "RVC headers found but RVC.lib missing under ${RVC_LIB_DIR}; using stub camera adapter")
  else()
    message(WARNING "RVC SDK not found at ${RVC_ROOT}; using stub camera adapter")
  endif()
endif()

# deploy_msvc_runtime.cmake
# 将 MSVC CRT 拷到 -DDST= 目录，便于纯净客户机无 VC 红包也能跑。
#
# 可选: -DFORCE_VC143=1
#   强制覆盖为 VS2022 (VC143) CRT。PointCloudProcessor / pcp_c_api 由 14.44 编译，
#   若旁路放置 VS2019 (VC142) CRT 会导致构造期访问冲突。
if(NOT DST)
  message(FATAL_ERROR "deploy_msvc_runtime.cmake requires -DDST=")
endif()
file(MAKE_DIRECTORY "${DST}")

set(_msvc_dlls
  msvcp140.dll
  msvcp140_1.dll
  msvcp140_2.dll
  vcruntime140.dll
  vcruntime140_1.dll
  concrt140.dll
  vcomp140.dll)

set(_prefer_vc143 OFF)
if(FORCE_VC143 OR FORCE_VC143 STREQUAL "1" OR FORCE_VC143 STREQUAL "ON")
  set(_prefer_vc143 ON)
endif()

set(_msvc_src_dirs "")

# VS2022 / VC143 红包（含 BuildTools）
file(GLOB _vs2022_crt
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
  "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT")
# 取版本号最高的目录（按路径字符串排序，14.44 > 14.42）
if(_vs2022_crt)
  list(SORT _vs2022_crt COMPARE NATURAL ORDER DESCENDING)
endif()

if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
  if(_prefer_vc143)
    list(APPEND _msvc_src_dirs "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
  else()
    list(APPEND _msvc_src_dirs
      "$ENV{VCToolsRedistDir}/x64/Microsoft.VC142.CRT"
      "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
  endif()
endif()

if(_prefer_vc143)
  list(APPEND _msvc_src_dirs ${_vs2022_crt})
else()
  list(APPEND _msvc_src_dirs
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
    ${_vs2022_crt}
    "C:/Windows/System32")
endif()

set(_msvc_copied 0)
foreach(_name IN LISTS _msvc_dlls)
  if(NOT _prefer_vc143 AND EXISTS "${DST}/${_name}")
    continue()
  endif()
  set(_found "")
  foreach(_dir IN LISTS _msvc_src_dirs)
    if(EXISTS "${_dir}/${_name}")
      set(_found "${_dir}/${_name}")
      break()
    endif()
  endforeach()
  if(_found STREQUAL "")
    continue()
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_found}" "${DST}/${_name}")
  math(EXPR _msvc_copied "${_msvc_copied}+1")
endforeach()
if(_msvc_copied GREATER 0)
  if(_prefer_vc143)
    message(STATUS "Overlaid ${_msvc_copied} MSVC VC143 runtime DLL(s) -> ${DST}")
  else()
    message(STATUS "Copied ${_msvc_copied} MSVC runtime DLL(s) -> ${DST}")
  endif()
endif()

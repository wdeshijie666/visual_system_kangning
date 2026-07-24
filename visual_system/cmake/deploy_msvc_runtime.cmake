# deploy_msvc_runtime.cmake
# 将 MSVC CRT 拷到 -DDST= 目录，便于纯净客户机无 VC 红包也能跑。
# 优先使用当前环境 VCToolsRedistDir，其次 VS2019 VC142，再次 VS2022 VC143 / System32。
if(NOT DST)
  message(FATAL_ERROR "deploy_msvc_runtime.cmake requires -DDST=")
endif()
file(MAKE_DIRECTORY "${DST}")

set(_msvc_dlls
  msvcp140.dll
  vcruntime140.dll
  vcruntime140_1.dll
  concrt140.dll
  vcomp140.dll)

set(_msvc_src_dirs "")
if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
  list(APPEND _msvc_src_dirs
    "$ENV{VCToolsRedistDir}/x64/Microsoft.VC142.CRT"
    "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
endif()
list(APPEND _msvc_src_dirs
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT")
file(GLOB _vs2022_crt
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
  "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT")
list(APPEND _msvc_src_dirs ${_vs2022_crt} "C:/Windows/System32")

set(_msvc_copied 0)
foreach(_name IN LISTS _msvc_dlls)
  if(EXISTS "${DST}/${_name}")
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
  message(STATUS "Copied ${_msvc_copied} MSVC runtime DLL(s) -> ${DST}")
endif()

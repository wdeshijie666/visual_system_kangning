# deploy_msvc_runtime.cmake
# 将较新 MSVC CRT 强制覆盖到 -DDST=。
# 原因：windeployqt --compiler-runtime / 旧 VC142(14.29) 会令 OpenCV 4.12
# 在 DllMain 初始化失败 → 0xc0000142（ERROR_DLL_INIT_FAILED）。
# 实测：VC143 ≥14.44 或本机 System32 可用；VC142 14.29 不可用。
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

# 只收 desktop x64 CRT，排除 onecore；显式优先 VS2022 BuildTools/Community
set(_msvc_src_dirs "")
list(APPEND _msvc_src_dirs
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT"
  "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT"
  "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT")
file(GLOB _vs2022_crt
  "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT")
list(REVERSE _vs2022_crt)
list(APPEND _msvc_src_dirs ${_vs2022_crt})
if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
  list(APPEND _msvc_src_dirs "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
endif()
# 注意：32 位 cmake 读 System32 可能被重定向到 SysWOW64，故放较后
list(APPEND _msvc_src_dirs "C:/Windows/System32")
# VC142 14.29 对 OpenCV4.12 会 0xc0000142，仅作无更新红包时的最后兜底
list(APPEND _msvc_src_dirs
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT")

set(_msvc_copied 0)
set(_msvc_from "")
foreach(_name IN LISTS _msvc_dlls)
  set(_found "")
  foreach(_dir IN LISTS _msvc_src_dirs)
    if(EXISTS "${_dir}/${_name}")
      set(_found "${_dir}/${_name}")
      if(_msvc_from STREQUAL "")
        set(_msvc_from "${_dir}")
      endif()
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
  message(STATUS "Forced ${_msvc_copied} MSVC runtime DLL(s) from ${_msvc_from} -> ${DST}")
endif()

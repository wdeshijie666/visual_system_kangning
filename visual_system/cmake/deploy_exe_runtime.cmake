# deploy_exe_runtime.cmake
# 将 VisualSystem.exe 运行依赖部署到 exe 同目录，便于整目录拷贝到客户机运行。
# 参数：-DEXE= -DQT_BIN= -DRVC_RUNTIME= -DPLCTAG_DLL=（可选）
if(NOT EXE OR NOT EXISTS "${EXE}")
  message(FATAL_ERROR "deploy_exe_runtime.cmake requires -DEXE= existing VisualSystem.exe")
endif()

get_filename_component(DST "${EXE}" DIRECTORY)

# 1) Qt 插件/依赖 + MSVC 运行库
if(QT_BIN AND EXISTS "${QT_BIN}/windeployqt.exe")
  set(_wdq_args --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw)
  # Release/Debug 由 exe 旁 PDB 粗判不够稳，默认按 Release 部署（产线包）
  list(APPEND _wdq_args --release)
  execute_process(
    COMMAND "${QT_BIN}/windeployqt.exe" ${_wdq_args} "${EXE}"
    WORKING_DIRECTORY "${DST}"
    RESULT_VARIABLE _wdq_rc
    OUTPUT_VARIABLE _wdq_out
    ERROR_VARIABLE _wdq_err)
  if(NOT _wdq_rc EQUAL 0)
    message(WARNING "windeployqt failed (rc=${_wdq_rc}): ${_wdq_err}")
  else()
    message(STATUS "windeployqt OK -> ${DST}")
  endif()
else()
  message(WARNING "windeployqt not found (QT_BIN=${QT_BIN}); Qt DLLs may be missing on customer PC")
endif()

# 2) RVC 相机运行时：RVC.dll 依赖同目录 MVCameraControl/GxIAPI 等，整包拷 DLL
if(RVC_RUNTIME AND EXISTS "${RVC_RUNTIME}")
  file(GLOB _rvc_dlls "${RVC_RUNTIME}/*.dll")
  set(_copied 0)
  foreach(_dll IN LISTS _rvc_dlls)
    get_filename_component(_name "${_dll}" NAME)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${DST}/${_name}")
    math(EXPR _copied "${_copied}+1")
  endforeach()
  message(STATUS "Copied ${_copied} RVC runtime DLL(s) from ${RVC_RUNTIME}")
endif()

# 3) libplctag（真机 PLC 时需要）
if(PLCTAG_DLL AND EXISTS "${PLCTAG_DLL}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLCTAG_DLL}" "${DST}/")
  message(STATUS "Copied plctag.dll")
endif()

# 4) MSVC 运行库（纯净机常无 VC++ 可再发行组件；windeployqt --compiler-runtime 在本环境常不生效）
set(_msvc_dlls
  msvcp140.dll
  vcruntime140.dll
  vcruntime140_1.dll
  concrt140.dll
  vcomp140.dll)
set(_msvc_src_dirs "")
if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
  list(APPEND _msvc_src_dirs "$ENV{VCToolsRedistDir}/x64/Microsoft.VC142.CRT")
  list(APPEND _msvc_src_dirs "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
endif()
list(APPEND _msvc_src_dirs
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
  "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT"
  "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC"
  "C:/Windows/System32")
# 2022 redist 版本号不固定，GLOB 一层
file(GLOB _vs2022_crt "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT")
list(APPEND _msvc_src_dirs ${_vs2022_crt})

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
  message(STATUS "Copied ${_msvc_copied} MSVC runtime DLL(s)")
endif()

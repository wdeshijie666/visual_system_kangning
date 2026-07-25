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

# 4) OpenCV 4.12（Stub 固定读 sim_test.tiff；仅 Release *4，跳过 *4d / *480）
if(OPENCV_BIN AND EXISTS "${OPENCV_BIN}")
  set(_cv_mods opencv_core4.dll opencv_imgproc4.dll opencv_imgcodecs4.dll)
  set(_cv_n 0)
  foreach(_name IN LISTS _cv_mods)
    if(EXISTS "${OPENCV_BIN}/${_name}")
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENCV_BIN}/${_name}" "${DST}/${_name}")
      math(EXPR _cv_n "${_cv_n}+1")
    endif()
  endforeach()
  # imgcodecs 常用解码依赖（注意 OpenCV4 链的是 z.dll，不是 zlib.dll）
  set(_codec_deps
    z.dll zlib.dll liblzma.dll libpng16.dll jpeg62.dll turbojpeg.dll tiff.dll libtiff.dll
    libwebp.dll libwebpdecoder.dll libwebpdemux.dll libwebpmux.dll libsharpyuv.dll)
  set(_codec_n 0)
  foreach(_name IN LISTS _codec_deps)
    if(EXISTS "${OPENCV_BIN}/${_name}")
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENCV_BIN}/${_name}" "${DST}/${_name}")
      math(EXPR _codec_n "${_codec_n}+1")
    endif()
  endforeach()
  # Release libwebp 覆盖（third_party/bin 可能是 Debug CRT）
  if(WEBP_RELEASE_DIR AND EXISTS "${WEBP_RELEASE_DIR}/libwebp.dll")
    foreach(_name IN ITEMS
        libwebp.dll libwebpdecoder.dll libwebpdemux.dll libwebpmux.dll libsharpyuv.dll)
      if(EXISTS "${WEBP_RELEASE_DIR}/${_name}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WEBP_RELEASE_DIR}/${_name}" "${DST}/${_name}")
      endif()
    endforeach()
    message(STATUS "Overlaid Release libwebp* from ${WEBP_RELEASE_DIR}")
  endif()
  message(STATUS "Copied OpenCV ${_cv_n} + codec ${_codec_n} DLL(s) from ${OPENCV_BIN}")
endif()

# 5) MSVC 运行库
set(_msvc_script "${CMAKE_CURRENT_LIST_DIR}/deploy_msvc_runtime.cmake")
if(EXISTS "${_msvc_script}")
  execute_process(COMMAND ${CMAKE_COMMAND} -DDST=${DST} -P "${_msvc_script}")
endif()

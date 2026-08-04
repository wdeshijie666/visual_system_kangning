# copy_plc_driver_dlls.cmake
# 将 SRC_DIR 下匹配 *_plc_driver.dll 的动态库拷到 DST（VisualSystem 等生成目录）。
# 参数：-DSRC_DIR= -DDST=
if(NOT DST)
  message(FATAL_ERROR "copy_plc_driver_dlls.cmake requires -DDST=")
endif()
if(NOT SRC_DIR OR NOT IS_DIRECTORY "${SRC_DIR}")
  return()
endif()

file(GLOB _plc_driver_dlls "${SRC_DIR}/*_plc_driver.dll")
foreach(_dll IN LISTS _plc_driver_dlls)
  get_filename_component(_name "${_dll}" NAME)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${DST}/${_name}"
    RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    message(STATUS "Copied ${_name} -> ${DST}")
  else()
    message(WARNING "Failed to copy ${_dll} -> ${DST} (rc=${_rc})")
  endif()
endforeach()

# 仅当目标不存在时拷贝 algo_config.json，避免覆盖现场修改。
# 变量：SRC, DST
# MSBuild/VERBATIM 可能把引号带进 -D 变量值，先去掉。

if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "copy_algo_config_if_missing.cmake requires SRC and DST")
endif()

foreach(_v SRC DST)
  string(REPLACE "\"" "" ${_v} "${${_v}}")
endforeach()

if(NOT EXISTS "${SRC}")
  message(FATAL_ERROR "algo_config source missing: ${SRC}")
endif()

if(NOT EXISTS "${DST}")
  get_filename_component(_dir "${DST}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  configure_file("${SRC}" "${DST}" COPYONLY)
  message(STATUS "Deployed algo_config.json -> ${DST}")
else()
  message(STATUS "Keep existing algo_config.json: ${DST}")
endif()

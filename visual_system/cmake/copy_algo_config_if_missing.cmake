# 仅当目标不存在时拷贝 algo_config.json，避免覆盖现场修改。
# 变量：SRC, DST

if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "copy_algo_config_if_missing.cmake requires SRC and DST")
endif()
if(NOT EXISTS "${DST}")
  get_filename_component(_dir "${DST}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  configure_file("${SRC}" "${DST}" COPYONLY)
endif()

# 同步 mock_algo_service 运行目录到 VisualSystem/alg_program。
# 若目标侧已有 algo_config.json，则保留现场修改，不被默认模板覆盖。
#
# 变量：MOCK_DIR, DST_DIR, DEFAULT_ALGO_CONFIG

if(NOT DEFINED MOCK_DIR OR NOT DEFINED DST_DIR OR NOT DEFINED DEFAULT_ALGO_CONFIG)
  message(FATAL_ERROR "sync_alg_program.cmake requires MOCK_DIR, DST_DIR, DEFAULT_ALGO_CONFIG")
endif()

# MSBuild/VERBATIM 可能把引号带进 -D 变量值，先去掉
foreach(_v MOCK_DIR DST_DIR DEFAULT_ALGO_CONFIG)
  string(REPLACE "\"" "" ${_v} "${${_v}}")
endforeach()

file(MAKE_DIRECTORY "${DST_DIR}")

set(_dst_cfg "${DST_DIR}/algo_config.json")
set(_bak "${DST_DIR}/.algo_config.user.json")
set(_had_user FALSE)
if(EXISTS "${_dst_cfg}")
  configure_file("${_dst_cfg}" "${_bak}" COPYONLY)
  set(_had_user TRUE)
endif()

file(GLOB _entries "${MOCK_DIR}/*")
foreach(_entry ${_entries})
  get_filename_component(_name "${_entry}" NAME)
  if(_name STREQUAL "algo_config.json" AND _had_user)
    continue()
  endif()
  file(COPY "${_entry}" DESTINATION "${DST_DIR}")
endforeach()

if(_had_user AND EXISTS "${_bak}")
  configure_file("${_bak}" "${_dst_cfg}" COPYONLY)
elseif(NOT EXISTS "${_dst_cfg}")
  configure_file("${DEFAULT_ALGO_CONFIG}" "${_dst_cfg}" COPYONLY)
endif()

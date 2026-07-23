# 本文件被顶层 CMakeLists include；记录脚本绝对路径供 POST_BUILD 使用。
set(VS_DEPLOY_EXE_RUNTIME_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/deploy_exe_runtime.cmake")

function(vs_copy_runtime_dlls target)
  if(NOT WIN32)
    return()
  endif()

  # Qt bin：与当前链接的 Qt5 一致
  set(_qt_bin "")
  if(TARGET Qt5::qmake)
    get_target_property(_qmake_loc Qt5::qmake IMPORTED_LOCATION)
    if(_qmake_loc)
      get_filename_component(_qt_bin "${_qmake_loc}" DIRECTORY)
    endif()
  endif()
  if(_qt_bin STREQUAL "" AND DEFINED Qt5_DIR)
    get_filename_component(_qt_bin "${Qt5_DIR}/../../../bin" ABSOLUTE)
  endif()

  set(_plctag "")
  if(DEFINED THIRD_PARTY_LIBRARY_DIR AND EXISTS "${THIRD_PARTY_LIBRARY_DIR}/libplctag/bin/plctag.dll")
    set(_plctag "${THIRD_PARTY_LIBRARY_DIR}/libplctag/bin/plctag.dll")
  endif()

  set(_rvc_runtime "")
  if(VISUAL_HAS_RVC_SDK)
    foreach(_cand IN ITEMS "${RVC_RUNTIME_DIR}" "${RVC_ROOT}/runtime" "${RVC_ROOT}/bin")
      if(EXISTS "${_cand}/RVC.dll")
        set(_rvc_runtime "${_cand}")
        break()
      endif()
    endforeach()
    if(_rvc_runtime STREQUAL "")
      message(WARNING "RVC.dll not found under RVC runtime/bin; camera may fail to load")
    endif()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DEXE=$<TARGET_FILE:${target}>"
      "-DQT_BIN=${_qt_bin}"
      "-DRVC_RUNTIME=${_rvc_runtime}"
      "-DPLCTAG_DLL=${_plctag}"
      -P "${VS_DEPLOY_EXE_RUNTIME_SCRIPT}"
    COMMENT "Deploy runtime DLLs beside ${target} (Qt/RVC/MSVC)"
    VERBATIM)
endfunction()

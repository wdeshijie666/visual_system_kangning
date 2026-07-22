function(vs_copy_runtime_dlls target)
  if(NOT WIN32)
    return()
  endif()

  set(_plctag "${THIRD_PARTY_LIBRARY_DIR}/libplctag/bin/plctag.dll")
  if(EXISTS "${_plctag}")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_plctag}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()

  if(NOT VISUAL_HAS_RVC_SDK)
    return()
  endif()

  # 只拷 RVC.dll（产线实测运行时仅依赖该库；整目录 runtime 含大量无关厂商 DLL）
  set(_rvc_dll "")
  foreach(_cand IN ITEMS "${RVC_RUNTIME_DIR}" "${RVC_ROOT}/runtime" "${RVC_ROOT}/bin")
    if(EXISTS "${_cand}/RVC.dll")
      set(_rvc_dll "${_cand}/RVC.dll")
      break()
    endif()
  endforeach()

  if(_rvc_dll STREQUAL "")
    message(WARNING "RVC.dll not found under RVC runtime/bin; camera may fail to load")
    return()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_rvc_dll}"
      "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Copy RVC.dll from ${_rvc_dll}"
    VERBATIM)
endfunction()

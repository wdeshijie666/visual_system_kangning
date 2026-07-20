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

  # RVC.dll 及 runtime 目录下依赖（如本 SDK：RVCSDK/runtime/）
  set(_rvc_candidates
      "${RVC_RUNTIME_DIR}/RVC.dll"
      "${RVC_ROOT}/runtime/RVC.dll"
      "${RVC_ROOT}/bin/RVC.dll"
      "${RVC_LIB_DIR}/RVC.dll"
  )
  foreach(_rvc_dll IN LISTS _rvc_candidates)
    if(EXISTS "${_rvc_dll}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rvc_dll}" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
      break()
    endif()
  endforeach()
endfunction()

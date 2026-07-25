# copy_release_dlls.cmake
# 从第三方 bin 拷贝 DLL 到算法进程目录，并尽量排除 Debug CRT 依赖。
# 必填: -DSRC= -DDST=
# 可选: -DSRC_RELEASE=  (Release 覆盖目录，优先于 SRC 同名 DLL)
# 可选: -DWEBP_RELEASE_DIR= (Release libwebp* 目录，供 OpenCV 4.12 imgcodecs)
if(NOT SRC OR NOT DST)
  message(FATAL_ERROR "copy_release_dlls.cmake requires -DSRC= and -DDST=")
endif()
file(MAKE_DIRECTORY "${DST}")

# ---------------------------------------------------------------------------
# 1) 从 SRC 拷贝，按文件名过滤明显 Debug 产物与 OpenCV 4.8（*480）
# ---------------------------------------------------------------------------
file(GLOB _dlls "${SRC}/*.dll")
set(_copied 0)
foreach(_dll IN LISTS _dlls)
  get_filename_component(_name "${_dll}" NAME)
  string(TOLOWER "${_name}" _name_l)
  # 仅按明确 Debug 命名过滤；勿用笼统 *d.dll（会误伤 zstd.dll 等）
  # OpenCV：算法与 mock_algo 统一 4.12（*4），跳过 4.8（*480）
  if(_name_l MATCHES "480d\\.dll$"
      OR _name_l MATCHES "opencv_.*480\\.dll$"
      OR _name_l MATCHES "4d\\.dll$"
      OR _name_l MATCHES "-9\\.3d\\.dll$"
      OR _name_l MATCHES "-gd-"
      OR _name_l MATCHES "^pcl_.+d\\.dll$"
      OR _name_l MATCHES "^qt6.+d\\.dll$"
      OR _name_l MATCHES "fmtd\\.dll$"
      OR _name_l MATCHES "flann_cppd\\.dll$"
      OR _name_l MATCHES "glew32d\\.dll$"
      OR _name_l MATCHES "freetyped\\.dll$"
      OR _name_l MATCHES "libexpatd\\.dll$"
      OR _name_l MATCHES "libpng16d\\.dll$"
      OR _name_l MATCHES "libprotobufd\\.dll$"
      OR _name_l MATCHES "qhull_rd\\.dll$"
      OR _name_l MATCHES "tiffd\\.dll$"
      OR _name_l MATCHES "^zd\\.dll$")
    continue()
  endif()
  file(COPY "${_dll}" DESTINATION "${DST}")
  math(EXPR _copied "${_copied}+1")
endforeach()
message(STATUS "Copied ${_copied} candidate DLLs from ${SRC} -> ${DST}")

# ---------------------------------------------------------------------------
# 2) Release 覆盖目录：同名 DLL 强制覆盖（pugixml/verdict/double-conversion 等）
# ---------------------------------------------------------------------------
if(SRC_RELEASE AND EXISTS "${SRC_RELEASE}")
  file(GLOB _rel_dlls "${SRC_RELEASE}/*.dll")
  set(_overlaid 0)
  foreach(_dll IN LISTS _rel_dlls)
    get_filename_component(_name "${_dll}" NAME)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${DST}/${_name}")
    math(EXPR _overlaid "${_overlaid}+1")
  endforeach()
  message(STATUS "Overlaid ${_overlaid} Release DLL(s) from ${SRC_RELEASE}")
endif()

# 清掉目录里残留的 OpenCV 4.8（含历史 core480 别名兼容文件）
file(GLOB _cv480_left "${DST}/opencv_*480.dll" "${DST}/opencv_*480d.dll")
foreach(_dll IN LISTS _cv480_left)
  get_filename_component(_name "${_dll}" NAME)
  file(REMOVE "${_dll}")
  message(STATUS "Removed OpenCV 4.8 leftover: ${_name}")
endforeach()

# ---------------------------------------------------------------------------
# 3) 删除仍依赖 Debug CRT 的 DLL（避免客户机缺 VCRUNTIME140D.dll）
# ---------------------------------------------------------------------------
find_program(_dumpbin NAMES dumpbin
  PATHS
    "$ENV{VCINSTALLDIR}/Tools/MSVC"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC"
    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
  PATH_SUFFIXES
    14.29.30133/bin/Hostx64/x64
    14.42.34433/bin/Hostx64/x64
    14.43.34808/bin/Hostx64/x64
    bin/Hostx64/x64
  NO_DEFAULT_PATH)
if(NOT _dumpbin)
  file(GLOB _db_candidates
    "C:/Program Files (x86)/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
    "C:/Program Files (x86)/Microsoft Visual Studio/*/Professional/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
    "C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
    "C:/Program Files/Microsoft Visual Studio/*/Professional/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe")
  if(_db_candidates)
    list(GET _db_candidates 0 _dumpbin)
  endif()
endif()

set(_removed_dbg 0)
if(_dumpbin)
  file(GLOB _dst_dlls "${DST}/*.dll")
  foreach(_dll IN LISTS _dst_dlls)
    get_filename_component(_name "${_dll}" NAME)
    string(TOLOWER "${_name}" _nl)
    # 本脚本部署的 Release CRT 自身跳过
    if(_nl STREQUAL "msvcp140.dll" OR _nl STREQUAL "vcruntime140.dll"
        OR _nl STREQUAL "vcruntime140_1.dll" OR _nl STREQUAL "concrt140.dll"
        OR _nl STREQUAL "vcomp140.dll")
      continue()
    endif()
    execute_process(
      COMMAND "${_dumpbin}" /DEPENDENTS "${_dll}"
      OUTPUT_VARIABLE _deps
      ERROR_VARIABLE _deps_err
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(TOUPPER "${_deps}" _deps_u)
    if(_deps_u MATCHES "VCRUNTIME140D\\.DLL"
        OR _deps_u MATCHES "MSVCP140D\\.DLL"
        OR _deps_u MATCHES "UCRTBASED\\.DLL"
        OR _deps_u MATCHES "VCRUNTIME140_1D\\.DLL")
      file(REMOVE "${_dll}")
      math(EXPR _removed_dbg "${_removed_dbg}+1")
      message(STATUS "Removed Debug-CRT DLL: ${_name}")
    endif()
  endforeach()
  message(STATUS "Removed ${_removed_dbg} Debug-CRT-linked DLL(s) from ${DST}")
else()
  message(WARNING "dumpbin not found; cannot purge Debug-CRT DLLs by dependency scan")
endif()

# ---------------------------------------------------------------------------
# 3.5) OpenCV 4.12 imgcodecs 依赖 libwebp*；third_party/bin 里多为 Debug CRT，
#      会被上一步删掉。改用已知 Release 包补齐。
# ---------------------------------------------------------------------------
set(_webp_names
  libwebp.dll libwebpdecoder.dll libwebpdemux.dll libwebpmux.dll libsharpyuv.dll)
set(_webp_src_dir "")
if(WEBP_RELEASE_DIR AND EXISTS "${WEBP_RELEASE_DIR}/libwebp.dll")
  set(_webp_src_dir "${WEBP_RELEASE_DIR}")
else()
  get_filename_component(_tp_root "${SRC}" DIRECTORY)
  get_filename_component(_recon_root "${_tp_root}" DIRECTORY)
  foreach(_cand IN ITEMS
      "${_recon_root}/Decode/Release"
      "${_recon_root}/third_party_v0/bin/Release")
    if(EXISTS "${_cand}/libwebp.dll")
      set(_webp_src_dir "${_cand}")
      break()
    endif()
  endforeach()
endif()
if(_webp_src_dir)
  set(_webp_n 0)
  foreach(_name IN LISTS _webp_names)
    if(EXISTS "${_webp_src_dir}/${_name}")
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_webp_src_dir}/${_name}" "${DST}/${_name}")
      math(EXPR _webp_n "${_webp_n}+1")
    endif()
  endforeach()
  message(STATUS "Deployed ${_webp_n} Release libwebp* from ${_webp_src_dir}")
else()
  message(WARNING "No Release libwebp* found; OpenCV4 imgcodecs may fail (missing lib*.dll)")
endif()

# ---------------------------------------------------------------------------
# 3.6) PointCloudProcessor 动态依赖 platform_diag.dll（仓库源码默认 STATIC，但现网
#      PCP 以 SHARED 链接）。third_party/bin 若已有则上面已拷贝；此处再做兜底与校验。
# ---------------------------------------------------------------------------
if(NOT EXISTS "${DST}/platform_diag.dll")
  set(_pd_candidates "${SRC}/platform_diag.dll")
  if(SRC_RELEASE)
    list(APPEND _pd_candidates "${SRC_RELEASE}/platform_diag.dll")
  endif()
  get_filename_component(_tp_root "${SRC}" DIRECTORY)
  get_filename_component(_recon_root "${_tp_root}" DIRECTORY)
  list(APPEND _pd_candidates
    "${_recon_root}/point_cloud/Release/platform_diag.dll")
  set(_pd_found "")
  foreach(_cand IN LISTS _pd_candidates)
    if(_cand AND EXISTS "${_cand}")
      set(_pd_found "${_cand}")
      break()
    endif()
  endforeach()
  if(_pd_found)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_pd_found}" "${DST}/platform_diag.dll")
    message(STATUS "Deployed platform_diag.dll from ${_pd_found}")
  else()
    message(WARNING
      "platform_diag.dll missing beside mock_algo_service; "
      "PointCloudProcessor.dll will fail to load (0xC0000135). "
      "Build SHARED platform_diag into ${SRC} or place the DLL next to the exe.")
  endif()
endif()

# ---------------------------------------------------------------------------
# 4) MSVC Release CRT（与当前工具集一致；勿强制 VC143，以免与 v142 引擎混用）
# ---------------------------------------------------------------------------
set(_msvc_script "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deploy_msvc_runtime.cmake")
if(EXISTS "${_msvc_script}")
  execute_process(COMMAND ${CMAKE_COMMAND} -DDST=${DST} -P "${_msvc_script}")
else()
  message(WARNING "deploy_msvc_runtime.cmake not found: ${_msvc_script}")
endif()

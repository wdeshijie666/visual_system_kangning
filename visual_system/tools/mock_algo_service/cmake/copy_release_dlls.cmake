# copy_release_dlls.cmake
# 从第三方 bin 拷贝 DLL 到算法进程目录，并尽量排除 Debug CRT 依赖。
# 必填: -DSRC= -DDST=
# 可选: -DSRC_RELEASE=  (Release 覆盖目录，优先于 SRC 同名 DLL)
if(NOT SRC OR NOT DST)
  message(FATAL_ERROR "copy_release_dlls.cmake requires -DSRC= and -DDST=")
endif()
file(MAKE_DIRECTORY "${DST}")

# ---------------------------------------------------------------------------
# 1) 从 SRC 拷贝，按文件名过滤明显 Debug 产物
# ---------------------------------------------------------------------------
file(GLOB _dlls "${SRC}/*.dll")
set(_copied 0)
foreach(_dll IN LISTS _dlls)
  get_filename_component(_name "${_dll}" NAME)
  string(TOLOWER "${_name}" _name_l)
  # 仅按明确 Debug 命名过滤；勿用笼统 *d.dll（会误伤 zstd.dll 等）
  if(_name_l MATCHES "480d\\.dll$"
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
# 4) MSVC Release CRT（算法进程强制 VC143，匹配 PointCloudProcessor/pcp_c_api）
# ---------------------------------------------------------------------------
set(_msvc_script "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deploy_msvc_runtime.cmake")
if(EXISTS "${_msvc_script}")
  execute_process(COMMAND ${CMAKE_COMMAND} -DDST=${DST} -DFORCE_VC143=1 -P "${_msvc_script}")
else()
  message(WARNING "deploy_msvc_runtime.cmake not found: ${_msvc_script}")
endif()

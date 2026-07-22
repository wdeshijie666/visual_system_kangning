# copy_release_dlls.cmake
# Copy Release DLLs; do NOT use a blanket "*d.dll" rule (would skip zstd.dll).
if(NOT SRC OR NOT DST)
  message(FATAL_ERROR "copy_release_dlls.cmake requires -DSRC= and -DDST=")
endif()
file(MAKE_DIRECTORY "${DST}")
file(GLOB _dlls "${SRC}/*.dll")
set(_copied 0)
foreach(_dll IN LISTS _dlls)
  get_filename_component(_name "${_dll}" NAME)
  # OpenCV Debug: *480d.dll / *4d.dll; VTK: *-9.3d.dll; Boost: *-gd-*; PCL: pcl_*d.dll
  if(_name MATCHES "480d\\.dll$" OR _name MATCHES "4d\\.dll$" OR _name MATCHES "-9\\.3d\\.dll$" OR _name MATCHES "-gd-" OR _name MATCHES "^pcl_.+d\\.dll$")
    continue()
  endif()
  file(COPY "${_dll}" DESTINATION "${DST}")
  math(EXPR _copied "${_copied}+1")
endforeach()
message(STATUS "Copied ${_copied} release DLLs from ${SRC} -> ${DST}")

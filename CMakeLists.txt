cmake_minimum_required (VERSION 3.16)
project (motif-on-qt VERSION 0.1 LANGUAGES C CXX)

# Motif on Qt: the Xt/Motif C API (and the part of Xlib Motif programs use)
# implemented with Qt widgets. Unmodified Motif programs compile against the
# genuine headers in include/ and link with -lXm -lXt -lX11, which all point
# at this one library. No X server and no libXm are needed at run time.

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_AUTOMOC OFF)

find_package (Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
  set (QT_WIDGETS Qt6::Widgets)
else ()
  message (FATAL_ERROR "Qt 6 (Widgets) is required")
endif ()

set (MQ_SOURCES
  src/xt_core.cpp
  src/xt_resources.cpp
  src/xt_events.cpp
  src/xlib.cpp
  src/xm_string.cpp
  src/xm_base.cpp
  src/xm_managers.cpp
  src/xm_text.cpp
  src/xm_dialogs.cpp
  src/xm_misc.cpp
  src/xm_records.c
  src/xt_selection.cpp
  src/mq_script.cpp
  src/xt_foreign.cpp)
set (MQ_WARNINGS -Wall -Wno-unused-parameter $<$<COMPILE_LANGUAGE:CXX>:-Wno-register> -Wno-misleading-indentation $<$<COMPILE_LANGUAGE:CXX>:-Wno-write-strings> -Wno-deprecated-declarations)

# The library is static: programs resolve every X/Xt/Motif symbol when they
# are linked, nothing is looked up at run time, and nothing is exported from
# the finished program (hidden visibility), so the system libX11 that Qt's
# own X11 support loads never meets these symbols. Qt stays a shared
# library, as for any Qt program.
add_library (motif-on-qt STATIC ${MQ_SOURCES})
target_include_directories (motif-on-qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE src)
target_compile_options (motif-on-qt PRIVATE ${MQ_WARNINGS})
target_link_libraries (motif-on-qt PUBLIC ${QT_WIDGETS})
set_target_properties (motif-on-qt PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Optional shared build (exports only the C API, in its own symbol version,
# and forwards foreign displays to the system libX11; see src/motif-on-qt.map)
option (MQ_BUILD_SHARED "Also build libmotif-on-qt.so" OFF)
if (MQ_BUILD_SHARED)
  add_library (motif-on-qt-shared SHARED ${MQ_SOURCES})
  target_include_directories (motif-on-qt-shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE src)
  target_compile_options (motif-on-qt-shared PRIVATE ${MQ_WARNINGS})
  target_link_libraries (motif-on-qt-shared PUBLIC ${QT_WIDGETS})
  target_link_options (motif-on-qt-shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/motif-on-qt.map -Wl,-Bsymbolic-functions)
  set_target_properties (motif-on-qt-shared PROPERTIES OUTPUT_NAME motif-on-qt VERSION ${PROJECT_VERSION} SOVERSION 0
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/motif-on-qt.map LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/shared)
endif ()

# Drop-in link names: lib/libXm.so, libXt.so, libX11.so ... are GNU ld
# scripts that pull in the archive and its Qt dependencies, so Makefiles that
# say -lXm -lXt -lX11 work unchanged with -L<libdir>. pkg-config files for
# x11, xt and friends point configure scripts at the same place.
set (MQ_LINK_NAMES Xm Xt X11 Xext Xmu Xpm ICE SM Mrm)
include (GNUInstallDirs)
set (MQ_INSTALL_LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR}/motif-on-qt)
set (MQ_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_FULL_INCLUDEDIR}/motif-on-qt)
set (MQ_QT_LIBS "-lQt6Widgets -lQt6Gui -lQt6Core")
# The C++ runtime the link scripts name for C programs: libstdc++, or
# LLVM's libc++ where that is the system's (Chimera, the BSDs ...)
include (CheckCXXSourceCompiles)
check_cxx_source_compiles ("#include <version>
#ifndef _LIBCPP_VERSION
#error not libc++
#endif
int main () { return 0; }" MQ_LIBCXX)
if (MQ_LIBCXX)
  set (MQ_CXX_RUNTIME "-lc++")
else ()
  set (MQ_CXX_RUNTIME "-lstdc++")
endif ()
function (mq_link_script file archive qtlibs)
  file (GENERATE OUTPUT ${file} CONTENT
"/* GNU ld script: Motif on Qt (the X11, Xt and Motif APIs on Qt 6). */
GROUP ( ${archive} )
INPUT ( ${qtlibs} ${MQ_CXX_RUNTIME} -lm -ldl )
")
endfunction ()
function (mq_pc_files dir libdir includedir)
  foreach (pc "x11|X11|1.8.7|-lX11" "xt|Xt|1.3.0|-lXt -lX11" "xext|Xext|1.3.6|-lXext" "xmu|Xmu|1.1.3|-lXmu"
               "xpm|Xpm|3.5.17|-lXpm" "ice|ICE|1.0.10|-lICE" "sm|SM|1.2.3|-lSM" "motif|Motif|2.3.8|-lXm -lXt -lX11"
               "motif-on-qt|Motif on Qt|${PROJECT_VERSION}|-lXm -lXt -lX11")
    string (REPLACE "|" ";" f "${pc}")
    list (GET f 0 name)
    list (GET f 1 desc)
    list (GET f 2 version)
    list (GET f 3 libs)
    file (WRITE ${dir}/${name}.pc
"libdir=${libdir}
includedir=${includedir}

Name: ${desc}
Description: ${desc} API provided by Motif on Qt
Version: ${version}
Cflags: -I\${includedir}
Libs: -L\${libdir} ${libs}
")
  endforeach ()
endfunction ()

# build tree: absolute paths to this build and to the Qt it was built with
set (MQ_BUILD_QT "$<TARGET_LINKER_FILE:Qt6::Widgets> $<TARGET_LINKER_FILE:Qt6::Gui> $<TARGET_LINKER_FILE:Qt6::Core>")
foreach (n ${MQ_LINK_NAMES})
  mq_link_script (${CMAKE_BINARY_DIR}/lib/lib${n}.so ${CMAKE_BINARY_DIR}/lib/libmotif-on-qt.a "${MQ_BUILD_QT}")
endforeach ()
mq_pc_files (${CMAKE_BINARY_DIR}/lib/pkgconfig ${CMAKE_BINARY_DIR}/lib ${CMAKE_CURRENT_SOURCE_DIR}/include)

# installed: /usr/lib/motif-on-qt/{libmotif-on-qt.a, libXm.so ..., pkgconfig/}
set (MQ_STAGE ${CMAKE_BINARY_DIR}/install-files)
foreach (n ${MQ_LINK_NAMES})
  mq_link_script (${MQ_STAGE}/lib${n}.so ${MQ_INSTALL_LIBDIR}/libmotif-on-qt.a "${MQ_QT_LIBS}")
endforeach ()
mq_pc_files (${MQ_STAGE}/pkgconfig ${MQ_INSTALL_LIBDIR} ${MQ_INSTALL_INCLUDEDIR})
set (MQ_LIBDIR ${MQ_INSTALL_LIBDIR})
set (MQ_INCLUDEDIR ${MQ_INSTALL_INCLUDEDIR})
configure_file (tools/motif-on-qt-env.in ${MQ_STAGE}/motif-on-qt-env @ONLY)
set (MQ_LIBDIR ${CMAKE_BINARY_DIR}/lib)
set (MQ_INCLUDEDIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
configure_file (tools/motif-on-qt-env.in ${CMAKE_BINARY_DIR}/motif-on-qt-env @ONLY
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Example and test programs: unmodified Motif sources
option (MQ_BUILD_EXAMPLES "Build the example Motif programs" ON)
if (MQ_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt)
  enable_testing ()
  add_subdirectory (examples)
endif ()

install (TARGETS motif-on-qt ARCHIVE DESTINATION ${MQ_INSTALL_LIBDIR})
foreach (n ${MQ_LINK_NAMES})
  install (FILES ${MQ_STAGE}/lib${n}.so DESTINATION ${MQ_INSTALL_LIBDIR})
endforeach ()
install (DIRECTORY ${MQ_STAGE}/pkgconfig DESTINATION ${MQ_INSTALL_LIBDIR})
install (FILES ${MQ_STAGE}/pkgconfig/motif-on-qt.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install (DIRECTORY include/ DESTINATION ${MQ_INSTALL_INCLUDEDIR})
install (PROGRAMS ${MQ_STAGE}/motif-on-qt-env DESTINATION ${CMAKE_INSTALL_BINDIR})
install (FILES README.md LICENSE.LGPL DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/motif-on-qt)

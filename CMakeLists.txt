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

add_library (motif-on-qt SHARED
  src/xt_core.cpp
  src/xt_resources.cpp
  src/xt_events.cpp
  src/xlib.cpp
  src/xm_string.cpp
  src/xm_base.cpp
  src/xm_managers.cpp
  src/xm_text.cpp
  src/xm_dialogs.cpp
  src/xm_misc.cpp)
target_include_directories (motif-on-qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE src)
target_compile_options (motif-on-qt PRIVATE -Wall -Wno-unused-parameter -Wno-register -Wno-misleading-indentation)
target_link_libraries (motif-on-qt PUBLIC ${QT_WIDGETS})
set_target_properties (motif-on-qt PROPERTIES
  VERSION ${PROJECT_VERSION} SOVERSION 0
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# libXm, libXt, libX11 ... as names for the same library, so Makefiles that
# say -lXm -lXt -lX11 work unchanged with -L<build>/lib
set (MQ_LINK_NAMES Xm Xt X11 Xext Xmu Xpm ICE SM Mrm)
foreach (n ${MQ_LINK_NAMES})
  add_custom_command (TARGET motif-on-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:motif-on-qt> ${CMAKE_BINARY_DIR}/lib/lib${n}.so)
endforeach ()

# Example and test programs: unmodified Motif sources
option (MQ_BUILD_EXAMPLES "Build the example Motif programs" ON)
if (MQ_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt)
  enable_testing ()
  add_subdirectory (examples)
endif ()

include (GNUInstallDirs)
install (TARGETS motif-on-qt LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/motif-on-qt)
install (DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/motif-on-qt)
foreach (n ${MQ_LINK_NAMES})
  install (CODE "execute_process (COMMAND ${CMAKE_COMMAND} -E create_symlink libmotif-on-qt.so
    \$ENV{DESTDIR}${CMAKE_INSTALL_FULL_LIBDIR}/motif-on-qt/lib${n}.so)")
endforeach ()

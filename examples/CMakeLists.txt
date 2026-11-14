# Unmodified Motif programs (from the Motif 2.3.8 demos, LGPL) built the way
# their own Makefiles would: plain C, the genuine headers, -lXm -lXt -lX11.
set (MQ_LIBDIR ${CMAKE_BINARY_DIR}/lib)

function (motif_program name)
  add_executable (${name} ${ARGN})
  set_target_properties (${name} PROPERTIES LINKER_LANGUAGE C)
  target_include_directories (${name} PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
  # gnu17: Motif-era code uses unprototyped functions, which C23 (GCC 15's
  # default) reads as taking no arguments
  target_compile_options (${name} PRIVATE -w -std=gnu17)
  add_dependencies (${name} motif-on-qt)
  set_target_properties (${name} PROPERTIES LINK_DEPENDS ${MQ_LIBDIR}/libmotif-on-qt.a)   # the link scripts hide this
  target_link_options (${name} PRIVATE -L${MQ_LIBDIR})
  target_link_libraries (${name} PRIVATE Xm Xt X11 m)   # the lib/lib*.so link scripts
  add_test (NAME ${name} COMMAND ${name})
  set_tests_properties (${name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MOTIFQT_AUTOQUIT=500;XAPPLRESDIR=${CMAKE_CURRENT_SOURCE_DIR}/"
    TIMEOUT 30)
endfunction ()

motif_program (tooltips tooltips.c)
motif_program (setdate setdate.c)
motif_program (earth earth.c)


motif_program (autopopups autopopups.c)

# Regression tests (our own small programs)
motif_program (formchain ${CMAKE_SOURCE_DIR}/tests/formchain.c)

# The headers compile as C++17 (C++ Motif programs such as xpdf)
add_executable (cxx-headers ${CMAKE_SOURCE_DIR}/tests/cxx-headers.cpp)
target_include_directories (cxx-headers PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties (cxx-headers PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options (cxx-headers PRIVATE -Werror=register)
target_link_libraries (cxx-headers PRIVATE motif-on-qt)

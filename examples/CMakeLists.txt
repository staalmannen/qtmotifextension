# Unmodified Motif programs (from the Motif 2.3.8 demos, LGPL) built the way
# their own Makefiles would: plain C, the genuine headers, -lXm -lXt -lX11.
set (MQ_LIBDIR ${CMAKE_BINARY_DIR}/lib)

function (motif_program name)
  add_executable (${name} ${ARGN})
  set_target_properties (${name} PROPERTIES LINKER_LANGUAGE C)
  target_include_directories (${name} PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options (${name} PRIVATE -w)
  add_dependencies (${name} motif-on-qt)
  target_link_options (${name} PRIVATE -L${MQ_LIBDIR} -Wl,-rpath,${MQ_LIBDIR})
  target_link_libraries (${name} PRIVATE Xm Xt X11 m)
  add_test (NAME ${name} COMMAND ${name})
  set_tests_properties (${name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MOTIFQT_AUTOQUIT=500;XAPPLRESDIR=${CMAKE_CURRENT_SOURCE_DIR}/"
    TIMEOUT 30)
endfunction ()

motif_program (tooltips tooltips.c)
motif_program (setdate setdate.c)
motif_program (earth earth.c)


motif_program (autopopups autopopups.c)

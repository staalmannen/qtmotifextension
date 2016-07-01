infile(config.pri, SOLUTIONS_LIBRARY, yes): CONFIG += qtmotifextension-uselib
TEMPLATE += fakelib
QTMOTIFEXTENSION_LIBNAME = $$qtLibraryTarget(QtSolutions_MotifExtension-2.7)
TEMPLATE -= fakelib
QTMOTIFEXTENSION_LIBDIR = $$PWD/lib
unix:qtmotifextension-uselib:!qtmotifextension-buildlib:QMAKE_RPATHDIR += $$QTMOTIFEXTENSION_LIBDIR

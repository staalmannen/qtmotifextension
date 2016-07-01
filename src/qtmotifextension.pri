include(../common.pri)
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
CONFIG += x11
LIBS += -lXm -lXt

qtmotifextension-uselib:!qtmotifextension-buildlib {
    LIBS += -L$$QTMOTIFEXTENSION_LIBDIR -l$$QTMOTIFEXTENSION_LIBNAME
} else {
    HEADERS += $$PWD/qtmotif.h \
               $$PWD/qtmotifwidget.h \
               $$PWD/qtmotifdialog.h \
               $$PWD/qtxtwidget.h
    SOURCES += $$PWD/qtmotif.cpp \
               $$PWD/qtmotifwidget.cpp \
               $$PWD/qtmotifdialog.cpp \
               $$PWD/qtxtwidget.cpp
}

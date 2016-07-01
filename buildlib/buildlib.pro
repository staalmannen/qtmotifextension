TEMPLATE=lib
CONFIG += qt dll qtmotifextension-buildlib
mac:CONFIG += absolute_library_soname
win32|mac:!wince*:!win32-msvc:!macx-xcode:CONFIG += debug_and_release build_all
include(../src/qtmotifextension.pri)
TARGET = $$QTMOTIFEXTENSION_LIBNAME
DESTDIR = $$QTMOTIFEXTENSION_LIBDIR
win32 {
    DLLDESTDIR = $$[QT_INSTALL_BINS]
    QMAKE_DISTCLEAN += $$[QT_INSTALL_BINS]\\$${QTMOTIFEXTENSION_LIBNAME}.dll
}
target.path = $$DESTDIR
INSTALLS += target

TEMPLATE=subdirs
CONFIG += ordered
QT += widgets gui
include(common.pri)
qtmotifextension-uselib:SUBDIRS=buildlib
SUBDIRS+=examples

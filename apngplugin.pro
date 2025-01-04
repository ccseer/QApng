QT       += core gui
CONFIG += plugin
TEMPLATE = lib
TARGET  = qapng
# overwrite version num
TARGET_EXT = .dll

include(libapng_static/libapng_static.pri)

HEADERS += \
    apnghandler.h \
    apngplugin.h

SOURCES += \
    apnghandler.cpp \
    apngplugin.cpp

OTHER_FILES += apng.json


QT += core gui widgets
TARGET = test
TEMPLATE = app 
TARGET = test 
SOURCES += test.cpp \
    ../apnghandler.cpp

include(../libapng_static/libapng_static.pri)

HEADERS += \
    ../apnghandler.h

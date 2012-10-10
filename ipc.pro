TEMPLATE = lib
DEV_ROOT = ./../..

include($$DEV_ROOT/src/base.pri)

# config
QT += core
QT -= gui

CONFIG += qt qt_no_framework
CONFIG -= app_bundle

#precompile header
PRECOMPILED_HEADER = stdafx.h

unix:LIBS += -lboost_system -lrt

# sources and headers
OTHER_FILES += \
    ../base.pri

HEADERS += \
    live_table.h \
    stdafx.h \
    client_impl.h \
    connected_state.h \
    ipc_common.h \
    ipc_helpers.h \
    shared_queue.h \
    cyclic_queue.h

SOURCES += \
    live_table.cpp \
    client_impl.cpp

QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = visualevent-tests
INCLUDEPATH += $$PWD/../include/core
SOURCES += $$PWD/visualeventscript_test.cpp $$PWD/../src/core/visualeventscript.cpp

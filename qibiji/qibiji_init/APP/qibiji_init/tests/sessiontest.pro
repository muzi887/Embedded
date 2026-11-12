QT += core serialport

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = sessiontest

INCLUDEPATH += ..

SOURCES += \
    main.cpp \
    ../devicesession.cpp \
    ../at4gconfigurator.cpp \
    ../sbf20codec.cpp

HEADERS += \
    ../devicesession.h \
    ../at4gconfigurator.h \
    ../sbf20codec.h

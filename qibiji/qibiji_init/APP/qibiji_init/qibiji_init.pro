QT += widgets serialport

CONFIG += c++17

TEMPLATE = app
TARGET = qibiji_init

SOURCES += \
    main.cpp \
    sbf20codec.cpp \
    devicesession.cpp \
    at4gconfigurator.cpp \
    mainwindow.cpp

HEADERS += \
    sbf20codec.h \
    devicesession.h \
    at4gconfigurator.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

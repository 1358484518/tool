QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

INCLUDEPATH += $$PWD $$PWD/common $$PWD/network

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    SerialAssistant.cpp \
    SerialManager.cpp \
    main.cpp \
    TMX_TOOL.cpp \
    network/FastTextView.cpp \
    network/NetAssistWidget.cpp \
    network/NetworkWorker.cpp \
    network/QTcpServerManager.cpp \
    network/QTcpSocketManager.cpp \
    network/QUdpSocketManager.cpp \
    qxymodem.cpp

HEADERS += \
    SerialAssistant.h \
    SerialManager.h \
    TMX_TOOL.h \
    common/ProtocolUtils.h \
    network/FastTextView.h \
    network/NetAssistWidget.h \
    network/NetCommon.h \
    network/NetworkWorker.h \
    network/QTcpServerManager.h \
    network/QTcpSocketManager.h \
    network/QUdpSocketManager.h \
    qxymodem.h

FORMS += \
    tmx_tool.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    image.qrc

RC_ICONS = tmx.ico

win32 {
    LIBS += -lws2_32 -lmswsock
}

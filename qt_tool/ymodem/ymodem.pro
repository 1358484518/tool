QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 源码按模块分目录：app 在根目录，串口/网络/公共工具各放各的目录
INCLUDEPATH += $$PWD $$PWD/common $$PWD/serial $$PWD/network

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    TMX_TOOL.cpp \
    serial/SerialAssistant.cpp \
    serial/SerialManager.cpp \
    serial/qxymodem.cpp \
    network/FastTextView.cpp \
    network/NetAssistWidget.cpp \
    network/NetworkWorker.cpp \
    network/QTcpServerManager.cpp \
    network/QTcpSocketManager.cpp \
    network/QUdpSocketManager.cpp

HEADERS += \
    TMX_TOOL.h \
    common/ProtocolUtils.h \
    common/IoData.h \
    serial/SerialAssistant.h \
    serial/SerialManager.h \
    serial/qxymodem.h \
    network/FastTextView.h \
    network/NetAssistWidget.h \
    network/NetCommon.h \
    network/NetworkWorker.h \
    network/QTcpServerManager.h \
    network/QTcpSocketManager.h \
    network/QUdpSocketManager.h

FORMS += \
    tmx_tool.ui

RESOURCES += \
    image.qrc

RC_ICONS = tmx.ico

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32 {
    LIBS += -lws2_32 -lmswsock
}

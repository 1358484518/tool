QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
VERSION = 2.06
# 传给 C++ 的是带引号的字符串宏，main 里用 QLatin1String(APP_VERSION) 取
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
# 程序下载页的脚本根目录，qmake 时按源码位置写死。斜杠避免进宏后被当成转义。
TFM_TOOLS_ROOT = $$clean_path($$PWD/../windows-tfm-tools)
TFM_TOOLS_ROOT ~= s,\\\\,/,g
DEFINES += TFM_TOOLS_DIR=\\\"$$TFM_TOOLS_ROOT\\\"

# 源码按模块分目录：app 在根目录，串口/网络/公共工具各放各的目录
INCLUDEPATH += $$PWD $$PWD/common $$PWD/serial $$PWD/network $$PWD/firmware $$PWD/download

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    TMX_TOOL.cpp \
    AppLanguage.cpp \
    SettingsWidget.cpp \
    serial/SerialAssistant.cpp \
    serial/SerialManager.cpp \
    serial/qxymodem.cpp \
    network/FastTextView.cpp \
    network/NetAssistWidget.cpp \
    network/NetworkWorker.cpp \
    network/QTcpServerManager.cpp \
    network/QTcpSocketManager.cpp \
    network/QUdpSocketManager.cpp \
    firmware/FirmwareWidget.cpp \
    firmware/FirmwarePassword.cpp \
    firmware/FileHashWidget.cpp \
    firmware/FirmwarePackWidget.cpp \
    download/ProgramDownloadWidget.cpp

HEADERS += \
    TMX_TOOL.h \
    AppLanguage.h \
    SettingsWidget.h \
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
    network/QUdpSocketManager.h \
    firmware/FirmwareWidget.h \
    firmware/FirmwarePassword.h \
    firmware/FileHashWidget.h \
    firmware/FirmwarePackWidget.h \
    download/ProgramDownloadWidget.h

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

# TMX 调试助手（Qt5）

基于 Qt5 的串口 / 网络调试工具，集成 YModem 文件发送。

## 目录

```
ymodem/
├── main.cpp / TMX_TOOL.* / tmx_tool.ui   主窗口
├── serial/                               串口助手、YModem
├── network/                              网络助手（TCP/UDP）
├── common/                               串口与网络共用工具
├── image/  miscfile/                     资源
└── ymodem.pro
```

## 架构

```
TMX_TOOL（主窗口）
└── QTabWidget
    ├── network/NetAssistWidget（网络 UI，主线程）
    │     └── NetworkWorker（QThread）
    │           ├── QTcpServerManager
    │           ├── QTcpSocketManager
    │           └── QUdpSocketManager
    └── serial/SerialAssistant（串口 UI，主线程）
          └── SerialManager（QThread）
                └── QYmodemFile（独立传输线程）
```

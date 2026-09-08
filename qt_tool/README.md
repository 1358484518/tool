# TMX 调试助手（Qt5）

基于 Qt5 的串口 / 网络调试工具，集成 YModem 文件发送。

## 架构

```
TMX_TOOL（主窗口）
└── QTabWidget
    ├── NetAssistWidget（网络 UI，主线程）
    │     └── NetworkWorker（QThread）
    │           ├── QTcpServerManager
    │           ├── QTcpSocketManager
    │           └── QUdpSocketManager
    └── SerialAssistant（串口 UI，主线程）
          └── SerialManager（QThread）
                └── QYmodemFile（独立传输线程）
```

- UI 与 IO 通过信号槽队列连接，避免阻塞界面。
- 串口/网络共用 `common/ProtocolUtils.h`（HEX、CRC16 Modbus、时间戳）。
- 网络收发日志使用 `FastTextView` 窗口化渲染，避免超大文本卡死。

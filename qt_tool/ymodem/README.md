# TMX 调试助手

Qt5 串口 + 网络调试工具，带 YModem 文件发送。UI 只负责显示和点按钮，真正的串口/套接字都在工作线程里。

目标编译环境：**Qt 5.14.2（MinGW 64-bit）**。云端验证用过 Qt 5.15。

## 目录

```
ymodem/
├── main.cpp                 入口
├── TMX_TOOL.h/.cpp/.ui      主窗口：页签、串口线程、YModem 桥接
├── ymodem.pro
├── common/                  串口和网络共用
│   ├── IoData.h             统一收包 IoPacket / IoSource
│   └── ProtocolUtils.h      HEX 编解码、CRC16 Modbus
├── serial/
│   ├── SerialAssistant      串口 UI
│   ├── SerialManager        串口后端（工作线程）
│   └── qxymodem.h           X/YModem 协议（独立传输线程）
└── network/
    ├── NetAssistWidget      网络 UI
    ├── NetworkWorker        网络后端（工作线程，按协议只活一个 Manager）
    ├── QTcpServerManager    TCP 服务端
    ├── QTcpSocketManager    TCP 客户端
    ├── QUdpSocketManager    UDP
    ├── FastTextView         大日志窗口（只画当前页，来包后下一事件循环刷新）
    └── NetCommon.h          协议枚举、对端地址解析
```

## 思路

三件事分开：

1. **界面**：`SerialAssistant`、`NetAssistWidget` 在 UI 线程，不创建 `QSerialPort` / `QTcpSocket`。
2. **后端**：`SerialManager`、`NetworkWorker` 各自一条 `QThread`，定时器和套接字都在这条线程上创建。
3. **数据口**：两边都继承 `IoSource`。收 `ioDataReceived(IoPacket)`，发 `sendIoData`。新控件只连这个对象，不要自己开关串口或套接字。

不要给工作线程上的对象设 Qt parent（有 parent 就不能 `moveToThread`）。对象创建后一直留在那条线程，退出时也在那条线程上 `delete`，不要再搬回 UI。

```
main
 └── TMX_TOOL                    UI 线程
      ├── NetAssistWidget        UI
      │    └── NetworkWorker     网络线程（无 parent）
      │         ├── QTcpServerManager
      │         ├── QTcpSocketManager
      │         └── QUdpSocketManager
      └── SerialAssistant        UI
           ├── SerialManager     串口线程（无 parent）
           └── QYmodemFile       自己的 QThread，parent = TMX_TOOL
```

## 线程与对象

| 对象 | 所在线程 | parent | 说明 |
|------|----------|--------|------|
| `TMX_TOOL` / 两个 UI | UI | 主窗口 | 只发信号、显示数据 |
| `SerialManager` | `m_serialThread` | 无（退出时在本线程 `delete`） | `QSerialPort`、定时器在 `initWorker()` 里创建 |
| `NetworkWorker` | `workerThread` | 无（退出时在本线程 `delete`） | 同一时刻只存在一种协议的 Manager |
| `QYmodemFile` | 自己的 `QThread::run` | `TMX_TOOL` | 和串口用信号收发字节，5 秒无应答退出 |

跨线程一律 `Qt::QueuedConnection`。退出时在工作线程里 `close` / `slotCloseNetwork` 然后 `delete` 后端（`BlockingQueuedConnection`），再 `quit` / `wait` 停空线程。`IoSource` 只负责收发，不管线程搬家。

## 流程

### 启动

1. `main` 注册 `NetProtocol`、`RemoteHost`，创建 `TMX_TOOL`。
2. `initSerialBackend`：`SerialManager` 无 parent → `moveToThread` → `started` 时 `initWorker`。
3. `initUi`：两个页签，串口 UI 信号Queued 到 `SerialManager`。
4. `NetAssistWidget::initNetWork`：同样把 `NetworkWorker` 丢到自己的线程。
5. `initYmodemBridge`：等用户点 YModem 再创建传输对象。

### 串口收发

```
点「打开」→ SerialAssistant::openPortRequested
         → TMX_TOOL 拼 SerialConfig
         → SerialManager::openWithConfig（串口线程）
         → QSerialPort::open

设备有数据 → SerialManager 拼 IoPacket
          → ioDataReceived → SerialAssistant::onIoData → 文本框

点「发送」→ sendDataRequested → IoSource::sendIoData → SerialManager 写出
```

其它控件：

```
connect(widget, &W::payloadReady, tmx->serialIoSource(),
        QOverload<const QByteArray &>::of(&IoSource::sendIoData), Qt::QueuedConnection);
connect(tmx->serialIoSource(), &IoSource::ioDataReceived, widget, &W::onIoData);
```

### 网络收发

```
点「打开」→ sigOpenNetwork → NetworkWorker::slotOpenNetwork
         → 按协议 new 一个 Manager（TCP 服务端 / 客户端 / UDP）

TCP 客户端再点「连接」→ slotTcpConnect → QTcpSocketManager::start
                      （客户端不 bind 本地监听口，重连时换新套接字）

收到数据 → NetworkWorker::forwardPayload
        → ioDataReceived → NetAssistWidget::onIoData

点「发送」→ sigSendData → sendIoData → 当前协议的 Manager
```

其它控件用 `tmx->networkIoSource()` 的 `sendIoData`。UDP/TCP 服务端可在 `IoPacket.peer/port` 里指定对端；TCP 客户端或广播则留空。

域名、`host:port`、`https://...` 在 `NetCommon.h` 的 `parseRemoteEndpoint` 里解析。UDP 发到主机名时用 `QHostInfo::fromName`。

### YModem

串口已打开时，选文件 → `QYmodemFile` 启动自己的线程。设备回的数据进 `receive()` 缓存，协议字节从 `send` 信号走 `SerialManager::sendBinary`。对端 5 秒不回则 `XMODEM_ERROR_IDLETIMEOUT`。

### 退出

对串口、网络各做一遍：

1. 断开 UI 与后端的信号。
2. `invokeMethod(..., BlockingQueued)` 在工作线程里关掉设备并 `delete` 后端。
3. `quit()` + `wait()` 停已经空了的工作线程。
4. YModem：`requestStop` → `wait` → `delete`（不要 `deleteLater`，会和 parent 析构撞车）。

## 类怎么管

- **主窗口 `TMX_TOOL`**：只组装，不实现协议。对外提供 `serialIoSource()` / `networkIoSource()`。
- **UI 类**：读控件、组配置、显示 `IoPacket`。不持有套接字。
- **Worker 类**：`SerialManager`、`NetworkWorker` 是唯一碰硬件的地方。`NetworkWorker` 切换协议时 `cleanupCurrentNet()`，三个 Manager 不同时存在。
- **Manager 类**：各自管连接、重连、发送队列。TCP 客户端 `stop()` 先断信号再 `abort`，避免重连定时器在退出时再 `bind`。
- **`IoSource`**：只做收 `ioDataReceived`、发 `sendIoData`。线程生命周期由主窗口 / 网络页析构处理，新控件不要自己开关链路。

## 编译

Qt Creator 打开 `ymodem.pro`，套件选 Qt 5.14.2 MinGW 64-bit。或命令行：

```
qmake ymodem.pro
mingw32-make   # Windows
make           # Linux
```

`moc_*.cpp`、`Makefile`、可执行文件不要提交。

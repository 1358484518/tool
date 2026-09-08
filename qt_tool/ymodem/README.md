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
    ├── FastTextView         大日志窗口（只画当前页）
    └── NetCommon.h          协议枚举、对端地址解析
```

## 思路

三件事分开：

1. **界面**：`SerialAssistant`、`NetAssistWidget` 在 UI 线程，不创建 `QSerialPort` / `QTcpSocket`。
2. **后端**：`SerialManager`、`NetworkWorker` 各自一条 `QThread`，定时器和套接字都在这条线程上创建。
3. **数据出口**：两边都继承 `IoSource`，收到数据只发 `ioDataReceived(IoPacket)`。其它控件连这一个信号即可。

不要给还在工作线程上的对象设 Qt parent（有 parent 就不能 `moveToThread`）。退出时必须先在工作线程里把对象“推”回 UI，再 `setParent`。

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
| `SerialManager` | `m_serialThread` | 运行时无；退出时交给主窗口 | `QSerialPort`、定时器在 `initWorker()` 里创建 |
| `NetworkWorker` | `workerThread` | 同上 | 同一时刻只存在一种协议的 Manager |
| `QYmodemFile` | 自己的 `QThread::run` | `TMX_TOOL` | 和串口用信号收发字节，5 秒无应答退出 |

跨线程一律 `Qt::QueuedConnection`。退出时 `close` / `slotCloseNetwork` / `handoverTo` 用 `BlockingQueuedConnection`，保证工作线程处理完再停。

`QObject::moveToThread` 只能从对象**当前线程**把对象推出去，不能从 UI 线程拉回来。所以 `IoSource::handoverTo` 必须在工作线程上调用。

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

点「发送」→ sendDataRequested → SerialManager::sendBinary
```

### 网络收发

```
点「打开」→ sigOpenNetwork → NetworkWorker::slotOpenNetwork
         → 按协议 new 一个 Manager（TCP 服务端 / 客户端 / UDP）

TCP 客户端再点「连接」→ slotTcpConnect → QTcpSocketManager::start
                      （客户端不 bind 本地监听口，重连时换新套接字）

收到数据 → NetworkWorker::forwardPayload
        → ioDataReceived + sigRecvData → NetAssistWidget::onIoData
```

域名、`host:port`、`https://...` 在 `NetCommon.h` 的 `parseRemoteEndpoint` 里解析。UDP 发到主机名时用 `QHostInfo::fromName`。

### YModem

串口已打开时，选文件 → `QYmodemFile` 启动自己的线程。设备回的数据进 `receive()` 缓存，协议字节从 `send` 信号走 `SerialManager::sendBinary`。对端 5 秒不回则 `XMODEM_ERROR_IDLETIMEOUT`。

### 退出

对串口、网络各做一遍：

1. `invokeMethod(close / slotCloseNetwork, BlockingQueued)` 先停设备。
2. `invokeMethod(handoverTo, BlockingQueued)` 在工作线程里 `moveToThread(UI)`。
3. `setParent(主窗口或网络页)`，交给 Qt 对象树释放。
4. `quit()` + `wait()` 停空线程。
5. YModem：`requestStop` → `wait` → `delete`（不要 `deleteLater`，会和 parent 析构撞车）。

## 类怎么管

- **主窗口 `TMX_TOOL`**：只组装，不实现协议。对外提供 `serialIoSource()` / `networkIoSource()`，方便以后再接控件。
- **UI 类**：读控件、组配置、显示 `IoPacket`。不持有套接字。
- **Worker 类**：`SerialManager`、`NetworkWorker` 是唯一碰硬件的地方。`NetworkWorker` 切换协议时 `cleanupCurrentNet()`，三个 Manager 不同时存在。
- **Manager 类**：各自管连接、重连、发送队列。TCP 客户端 `stop()` 先断信号再 `abort`，避免重连定时器在退出时再 `bind`。
- **`IoSource`**：基类只做两件事——发收包、退出时把线程亲和性交回 UI。

## 编译

Qt Creator 打开 `ymodem.pro`，套件选 Qt 5.14.2 MinGW 64-bit。或命令行：

```
qmake ymodem.pro
mingw32-make   # Windows
make           # Linux
```

`moc_*.cpp`、`Makefile`、可执行文件不要提交。

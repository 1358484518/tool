#include "example_common.h"
//通用接口示例程序
ExampleCommon::ExampleCommon(QObject *parent)
    : QObject{parent}
{
    m_comm = new CommunicationManager(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(3000); // Auto reconnect every 3s
    connect(m_reconnectTimer, &QTimer::timeout, this, &ExampleCommon::onReconnectTimeout);

    // Connect core communication signals
    connect(m_comm, &CommunicationManager::connected, this, &ExampleCommon::onCommConnected);
    connect(m_comm, &CommunicationManager::disconnected, this, &ExampleCommon::onCommDisconnected);
    connect(m_comm, &CommunicationManager::dataReceived, this, &ExampleCommon::onCommDataReceived);
    connect(m_comm, &CommunicationManager::errorOccurred, this, &ExampleCommon::onCommError);
}

ExampleCommon::~ExampleCommon()
{
    m_comm->close(false);
}

void ExampleCommon::connectToSerial(const QString &portName, qint32 baudrate)
{
    CommConfig cfg;
    cfg.type = CommConfig::Serial;
    cfg.serialPort = portName;
    cfg.baudRate = baudrate;
    cfg.dataBits = QSerialPort::Data8;
    cfg.parity = QSerialPort::NoParity;
    cfg.stopBits = QSerialPort::OneStop;
    cfg.flowControl = QSerialPort::NoFlowControl;
    startConnect(cfg);
}

void ExampleCommon::connectToTcp(const QString &host, quint16 port)
{
    CommConfig cfg;
    cfg.type = CommConfig::Tcp;
    cfg.tcpHost = host;
    cfg.tcpPort = port;
    cfg.lowLatencyMode = true;
    cfg.tcpKeepAlive = true;
    cfg.connectTimeoutMs = 5000;
    startConnect(cfg);
}

void ExampleCommon::connectToWebSocket(const QString &url, const QString &subProtocol, bool ignoreSslErrors)
{
    CommConfig cfg;
    cfg.type = CommConfig::WebSocket;
    cfg.wsUrl = url;
    cfg.wsSubProtocol = subProtocol;
    cfg.wsIgnoreSslErrors = ignoreSslErrors;
    cfg.wsPingIntervalMs = 30000;
    cfg.connectTimeoutMs = 5000;
    startConnect(cfg);
}

void ExampleCommon::disconnect(bool graceful)
{
    m_manualDisconnect = true;
    m_reconnectTimer->stop();
    m_comm->close(graceful);
}

qint64 ExampleCommon::sendRaw(const QByteArray &data)
{
    return m_comm->send(data);
}

qint64 ExampleCommon::sendHex(const QString &hexString)
{
    QByteArray data = hexStringToBytes(hexString);
    if (data.isEmpty()) {
        emit errorOccurred("Invalid hex string format");
        return -1;
    }
    qint64 ret = m_comm->send(data);
    if (ret > 0) {
        emit logMessage(QString("TX: %1").arg(bytesToHexString(data)), "#008800");
    }
    return ret;
}

CommState ExampleCommon::state() const
{
    return m_comm->state();
}

QByteArray ExampleCommon::hexStringToBytes(const QString &hex)
{
    QString hexStr = hex.simplified().remove(' ');
    QByteArray bytes;
    for (int i = 0; i < hexStr.size(); i += 2) {
        bool ok;
        bytes.append(static_cast<char>(hexStr.mid(i, 2).toUInt(&ok, 16)));
        if (!ok) return QByteArray();
    }
    return bytes;
}

QString ExampleCommon::bytesToHexString(const QByteArray &bytes)
{
    QString hex;
    for (unsigned char c : bytes) {
        hex += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
    }
    return hex.trimmed();
}

void ExampleCommon::onCommConnected()
{
    m_reconnectTimer->stop();
    emit logMessage("Connection success", "#008800");
    emit connected();
}

void ExampleCommon::onCommDisconnected()
{
    emit logMessage("Disconnected", "#ff0000");
    emit disconnected();
    // Start auto reconnect only for abnormal disconnect
    if (!m_manualDisconnect) {
        m_reconnectTimer->start();
    }
}

void ExampleCommon::onCommDataReceived(const QByteArray &data)
{
    QString hex = bytesToHexString(data);
    emit logMessage(QString("RX: %1").arg(hex), "#000000");
    emit dataReceived(data, hex);
}

void ExampleCommon::onCommError(const QString &err)
{
    emit logMessage(QString("ERROR: %1").arg(err), "#ff0000");
    emit errorOccurred(err);
}

void ExampleCommon::onReconnectTimeout()
{
    if (m_comm->state() != Unconnected || m_manualDisconnect) return;
    emit logMessage("Auto reconnecting...", "#888888");
    m_comm->open(m_lastConfig);
}

void ExampleCommon::startConnect(const CommConfig &cfg)
{
    m_manualDisconnect = false;
    m_lastConfig = cfg;
    m_reconnectTimer->stop();
    // Cleanup existing connection
    if (m_comm->state() != Unconnected) {
        m_comm->close(false);
    }
    emit logMessage("Start connecting...", "#0000ff");
    m_comm->open(cfg);
}
#if 0
// 1. 初始化实例
ExampleCommon* comm = new ExampleCommon(this);

// 2. 绑定信号到UI/业务逻辑
connect(comm, &ExampleCommon::logMessage, this, [this](const QString& msg, const QString& color){
    ui->logView->appendHtml(QString("<font color='%1'>%2</font>").arg(color, msg));
});
connect(comm, &ExampleCommon::dataReceived, this, [](const QByteArray& raw, const QString& hex){
    // 业务解析：raw直接喂给Ymodem/Modbus/OCPP解析库，无需关心底层连接类型
    // ymodem_feed(raw.constData(), raw.size());
    // modbus_parse(raw.constData(), raw.size());
});

// 3. 调用连接接口（三选一）
comm->connectToSerial("COM3", 115200);          // 串口连接STM32
comm->connectToTcp("192.168.1.100", 502);      // TCP连接Modbus设备
comm->connectToWebSocket("wss://ocpp.example.com/CP001"); // WebSocket连接OCPP后台

// 发送数据（支持直接传十六进制字符串）
comm->sendHex("01 03 00 00 00 0A C5 CD");

// 手动断开
comm->disconnect();
#endif

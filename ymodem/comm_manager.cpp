#include "comm_manager.h"

CommunicationManager::CommunicationManager(QObject *parent)
    : QObject{parent}
{
}

CommunicationManager::~CommunicationManager()
{
    close();
    cleanupDevice();
}

bool CommunicationManager::open(const CommConfig &config)
{
    // Close existing connection first
    if (m_state != Unconnected) {
        close();
    }
    m_config = config;
    cleanupDevice();

    if (config.type == CommConfig::Serial) {
        // Create serial port instance
        QSerialPort* serial = new QSerialPort(this);
        serial->setPortName(config.serialPort);
        serial->setBaudRate(config.baudRate);
        serial->setDataBits(config.dataBits);
        serial->setParity(config.parity);
        serial->setStopBits(config.stopBits);
        serial->setFlowControl(config.flowControl);

        m_device = serial;
        m_state = Connecting;

        // Connect signals
        connect(serial, &QSerialPort::readyRead, this, &CommunicationManager::onReadyRead);
        connect(serial, &QSerialPort::errorOccurred, this, &CommunicationManager::onDeviceError);
        connect(serial, &QSerialPort::aboutToClose, this, [this](){
            m_state = Unconnected;
            emit disconnected();
        });

        // Open serial (synchronous, unify to async signal)
        if (!serial->open(QIODevice::ReadWrite)) {
            m_state = Error;
            emit errorOccurred(QString("Serial open failed: %1").arg(serial->errorString()));
            return false;
        }

        m_state = Connected;
        QTimer::singleShot(0, this, [this](){
            emit connected();
        });
        return true;
    }
    else if (config.type == CommConfig::Tcp) {
        // Create TCP socket instance
        QTcpSocket* socket = new QTcpSocket(this);
        m_device = socket;
        m_state = Connecting;

        // Connect signals
        connect(socket, &QTcpSocket::connected, this, [this](){
            m_state = Connected;
            emit connected();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this](){
            m_state = Unconnected;
            emit disconnected();
        });
        connect(socket, &QTcpSocket::readyRead, this, &CommunicationManager::onReadyRead);
        connect(socket, &QTcpSocket::errorOccurred, this, &CommunicationManager::onTcpError);

        // Connect to host (async)
        socket->connectToHost(config.tcpHost, config.tcpPort);
        return true;
    }
    else if (config.type == CommConfig::WebSocket) {
        // New: Create WebSocket instance
        QWebSocket* ws = new QWebSocket(config.wsSubProtocol, QWebSocketProtocol::VersionLatest, this);
        m_device = ws;
        m_state = Connecting;

        // SSL error ignore (for self-signed certificate debug)
        if (config.wsIgnoreSslErrors) {
            connect(ws, &QWebSocket::sslErrors, this, [ws](const QList<QSslError>& errors){
                ws->ignoreSslErrors(errors);
            });
        }

        // Connect signals
        connect(ws, &QWebSocket::connected, this, [this](){
            m_state = Connected;
            emit connected();
        });
        connect(ws, &QWebSocket::disconnected, this, [this](){
            m_state = Unconnected;
            emit disconnected();
        });
        // Binary message (for industrial protocol/OCPP)
        connect(ws, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& data){
            if (!data.isEmpty()) {
                emit dataReceived(data);
            }
        });
        // Text message compatibility
        connect(ws, &QWebSocket::textMessageReceived, this, [this](const QString& msg){
            emit dataReceived(msg.toUtf8());
        });
        // Error signal compatible with Qt5/Qt6
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        connect(ws, &QWebSocket::errorOccurred, this, &CommunicationManager::onWsError);
#else
        connect(ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &CommunicationManager::onWsError);
#endif

        // Open WebSocket connection (async, support ws/wss)
        ws->open(QUrl(config.wsUrl));
        return true;
    }

    emit errorOccurred("Unsupported communication type");
    return false;
}

void CommunicationManager::close()
{
    if (m_state == Unconnected || !m_device) return;

    if (m_config.type == CommConfig::Serial) {
        QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
        if (serial) serial->close();
    }
    else if (m_config.type == CommConfig::Tcp) {
        QTcpSocket* socket = qobject_cast<QTcpSocket*>(m_device);
        if (socket) socket->close();
    }
    else if (m_config.type == CommConfig::WebSocket) {
        QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
        if (ws) ws->close(); // Send WebSocket close frame per RFC
    }

    m_state = Unconnected;
}

qint64 CommunicationManager::send(const QByteArray &data)
{
    if (m_state != Connected || !m_device) {
        emit errorOccurred("Device not connected, send failed");
        return -1;
    }

    if (m_config.type == CommConfig::Serial || m_config.type == CommConfig::Tcp) {
        QIODevice* ioDev = qobject_cast<QIODevice*>(m_device);
        return ioDev ? ioDev->write(data) : -1;
    }
    else if (m_config.type == CommConfig::WebSocket) {
        QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
        return ws ? ws->sendBinaryMessage(data) : -1;
    }

    return -1;
}

CommState CommunicationManager::state() const
{
    return m_state;
}

void CommunicationManager::onReadyRead()
{
    QIODevice* ioDev = qobject_cast<QIODevice*>(m_device);
    if (!ioDev) return;
    // Read all available data at once, avoid missing bytes
    const QByteArray data = ioDev->readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
    }
}

void CommunicationManager::onDeviceError(QSerialPort::SerialPortError serialErr)
{
    QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
    if (serialErr == QSerialPort::NoError || !serial) return;

    m_state = Error;
    emit errorOccurred(QString("Serial error: %1").arg(serial->errorString()));
    close();
}

void CommunicationManager::onTcpError(QAbstractSocket::SocketError tcpErr)
{
    Q_UNUSED(tcpErr)
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(m_device);
    if (!socket) return;

    m_state = Error;
    emit errorOccurred(QString("TCP error: %1").arg(socket->errorString()));
    close();
}

void CommunicationManager::onWsError(QAbstractSocket::SocketError wsErr)
{
    Q_UNUSED(wsErr)
    QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
    if (!ws) return;

    m_state = Error;
    emit errorOccurred(QString("WebSocket error: %1").arg(ws->errorString()));
    close();
}

void CommunicationManager::cleanupDevice()
{
    if (m_device) {
        m_device->deleteLater();
        m_device = nullptr;
    }
    m_state = Unconnected;
}


#if 0

// Configure WSS connection for OCPP
CommConfig cfg;
cfg.type = CommConfig::WebSocket;
cfg.wsUrl = "wss://ocpp-backend.example.com/CP/CHARGER001";
cfg.wsSubProtocol = "ocpp1.6"; // Match OCPP subprotocol
cfg.wsIgnoreSslErrors = true; // Debug only, disable in production

// Signal connection is EXACTLY same as serial/TCP
connect(m_comm, &CommunicationManager::connected, this, [](){
    qDebug() << "OCPP server connected";
    // Send BootNotification after connected
});
connect(m_comm, &CommunicationManager::dataReceived, this, [](const QByteArray& data){
    qDebug() << "OCPP RX:" << data;
    // Parse OCPP JSON here, no need to modify protocol code
});
connect(m_comm, &CommunicationManager::errorOccurred, this, [](const QString& err){
    qDebug() << "Connect error:" << err;
});

m_comm->open(cfg);
// Send OCPP message (same send API as serial/TCP)
m_comm->send("{\"chargePointModel\":\"H573-Charger\"}");

#endif

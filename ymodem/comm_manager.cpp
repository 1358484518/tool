#include "comm_manager.h"

CommunicationManager::CommunicationManager(QObject *parent)
    : QObject{parent}
{
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &CommunicationManager::onConnectTimeout);

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &QTimer::timeout, this, &CommunicationManager::onGracefulCloseTimeout);

    // Manual WebSocket ping timer (compatible with Qt <5.15, same as setPingInterval)
    m_wsPingTimer = new QTimer(this);
    connect(m_wsPingTimer, &QTimer::timeout, this, [this](){
        if (m_state != Connected || m_config.type != CommConfig::WebSocket) return;
        QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
        if (ws) ws->ping();
    });
}

CommunicationManager::~CommunicationManager()
{
    close(false);
    cleanupDevice();
}

bool CommunicationManager::open(const CommConfig &config)
{
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();

    if (m_state != Unconnected) {
        forceAbort();
        cleanupDevice();
    }
    m_config = config;

    if (config.type == CommConfig::Serial) {
        if (config.serialPort.isEmpty() || config.baudRate <= 0) {
            emit errorOccurred("Serial config invalid: empty port or invalid baudrate");
            return false;
        }

        QSerialPort* serial = new QSerialPort(this);
        serial->setPortName(config.serialPort);
        serial->setBaudRate(config.baudRate);
        serial->setDataBits(config.dataBits);
        serial->setParity(config.parity);
        serial->setStopBits(config.stopBits);
        serial->setFlowControl(config.flowControl);

        m_device = serial;
        m_state = Connecting;

        connect(serial, &QSerialPort::readyRead, this, &CommunicationManager::onReadyRead);
        connect(serial, &QSerialPort::errorOccurred, this, &CommunicationManager::onDeviceError);
        connect(serial, &QSerialPort::aboutToClose, this, &CommunicationManager::handleDisconnect);

        if (!serial->open(QIODevice::ReadWrite)) {
            m_state = Error;
            emit errorOccurred(QString("Serial open failed: %1").arg(serial->errorString()));
            cleanupDevice();
            return false;
        }

        serial->clear(QSerialPort::AllDirections);
        m_state = Connected;
        m_connectTimer->stop();
        QTimer::singleShot(0, this, [this](){
            emit connected();
        });
        return true;
    }
    else if (config.type == CommConfig::Tcp) {
        if (config.tcpHost.isEmpty() || config.tcpPort == 0) {
            emit errorOccurred("TCP config invalid: empty host or port");
            return false;
        }

        QTcpSocket* socket = new QTcpSocket(this);
        m_device = socket;
        m_state = Connecting;
        m_connectTimer->start(config.connectTimeoutMs);

        connect(socket, &QTcpSocket::connected, this, [this, socket](){
            if (m_state != Connecting) return;
            if (m_config.lowLatencyMode) {
                socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            }
            if (m_config.tcpKeepAlive) {
                socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
            }
            m_state = Connected;
            m_connectTimer->stop();
            emit connected();
        });
        connect(socket, &QTcpSocket::disconnected, this, &CommunicationManager::handleDisconnect);
        connect(socket, &QTcpSocket::readyRead, this, &CommunicationManager::onReadyRead);
// Qt5.14/5.15+ compatible TCP error signal
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        connect(socket, &QAbstractSocket::errorOccurred, this, &CommunicationManager::onTcpError);
#else
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &CommunicationManager::onTcpError);
#endif

        socket->connectToHost(config.tcpHost, config.tcpPort);
        return true;
    }
    else if (config.type == CommConfig::WebSocket) {
        QUrl wsUrl(config.wsUrl);
        if (!wsUrl.isValid() || (wsUrl.scheme() != "ws" && wsUrl.scheme() != "wss")) {
            emit errorOccurred("WebSocket config invalid: url must start with ws:// or wss://");
            return false;
        }

        QWebSocket* ws = new QWebSocket(config.wsSubProtocol, QWebSocketProtocol::VersionLatest, this);
        m_device = ws;
        m_state = Connecting;
        m_connectTimer->start(config.connectTimeoutMs);

        if (config.wsIgnoreSslErrors) {
            connect(ws, &QWebSocket::sslErrors, this, [ws](const QList<QSslError>& errors){
                ws->ignoreSslErrors(errors);
            });
        }

        connect(ws, &QWebSocket::connected, this, [this](){
            if (m_state != Connecting) return;
            m_state = Connected;
            m_connectTimer->stop();
            // Start auto ping timer, same behavior as setPingInterval
            m_wsPingTimer->start(m_config.wsPingIntervalMs);
            emit connected();
        });
        connect(ws, &QWebSocket::disconnected, this, [this](){
            m_wsPingTimer->stop();
            handleDisconnect();
        });
        connect(ws, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& data){
            if (!data.isEmpty() && m_state == Connected) {
                emit dataReceived(data);
            }
        });
        connect(ws, &QWebSocket::textMessageReceived, this, [this](const QString& msg){
            if (m_state == Connected) {
                emit dataReceived(msg.toUtf8());
            }
        });
// Qt5.14/5.15+ compatible WebSocket error signal
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        connect(ws, &QWebSocket::errorOccurred, this, &CommunicationManager::onWsError);
#else
        connect(ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &CommunicationManager::onWsError);
#endif

        ws->open(wsUrl);
        return true;
    }

    emit errorOccurred("Unsupported communication type");
    return false;
}

void CommunicationManager::close(bool graceful)
{
    if (m_state == Unconnected) return;
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();

    if (!m_device) {
        m_state = Unconnected;
        return;
    }

    if (graceful && m_state == Connected) {
        m_state = Closing;
        m_closeTimer->start(m_config.gracefulCloseTimeoutMs);

        if (m_config.type == CommConfig::Serial) {
            QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
            if (serial && serial->isOpen()) serial->close();
        }
        else if (m_config.type == CommConfig::Tcp) {
            QTcpSocket* socket = qobject_cast<QTcpSocket*>(m_device);
            if (socket) socket->disconnectFromHost();
        }
        else if (m_config.type == CommConfig::WebSocket) {
            QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
            if (ws) ws->close(QWebSocketProtocol::CloseCodeNormal);
        }
    } else {
        forceAbort();
        handleDisconnect();
    }
}

qint64 CommunicationManager::send(const QByteArray &data)
{
    if (m_state != Connected || !m_device) {
        if (!data.isEmpty()) {
            emit errorOccurred("Device not connected, send failed");
        }
        return data.isEmpty() ? 0 : -1;
    }

    if (data.isEmpty()) return 0;

    if (m_config.type == CommConfig::Serial) {
        QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
        if (!serial) return -1;
        qint64 ret = serial->write(data);
        if (m_config.lowLatencyMode) serial->flush();
        return ret;
    }
    else if (m_config.type == CommConfig::Tcp) {
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
    if (m_state != Connected) return;
    QIODevice* ioDev = qobject_cast<QIODevice*>(m_device);
    if (!ioDev) return;

    const QByteArray data = ioDev->readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
    }
}

void CommunicationManager::onDeviceError(QSerialPort::SerialPortError serialErr)
{
    QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
    if (serialErr == QSerialPort::NoError || !serial || m_state == Error) return;

    m_state = Error;
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();
    emit errorOccurred(QString("Serial error: %1").arg(serial->errorString()));
    close(false);
}

void CommunicationManager::onTcpError(QAbstractSocket::SocketError tcpErr)
{
    Q_UNUSED(tcpErr)
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(m_device);
    if (!socket || m_state == Error) return;

    m_state = Error;
    m_connectTimer->stop();
    m_closeTimer->stop();
    emit errorOccurred(QString("TCP error: %1").arg(socket->errorString()));
    close(false);
}

void CommunicationManager::onWsError(QAbstractSocket::SocketError wsErr)
{
    Q_UNUSED(wsErr)
    QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
    if (!ws || m_state == Error) return;

    m_state = Error;
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();
    emit errorOccurred(QString("WebSocket error: %1").arg(ws->errorString()));
    close(false);
}

void CommunicationManager::onConnectTimeout()
{
    if (m_state != Connecting) return;
    m_state = Error;
    m_wsPingTimer->stop();
    emit errorOccurred("Connection timeout");
    close(false);
}

void CommunicationManager::onGracefulCloseTimeout()
{
    if (m_state != Closing) return;
    m_wsPingTimer->stop();
    forceAbort();
    handleDisconnect();
}

void CommunicationManager::handleDisconnect()
{
    if (m_state == Unconnected) return;
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();
    m_state = Unconnected;
    emit disconnected();
}

void CommunicationManager::forceAbort()
{
    if (!m_device) return;

    if (m_config.type == CommConfig::Serial) {
        QSerialPort* serial = qobject_cast<QSerialPort*>(m_device);
        if (serial && serial->isOpen()) serial->close();
    }
    else if (m_config.type == CommConfig::Tcp) {
        QTcpSocket* socket = qobject_cast<QTcpSocket*>(m_device);
        if (socket) {
            socket->abort();
            socket->close();
        }
    }
    else if (m_config.type == CommConfig::WebSocket) {
        QWebSocket* ws = qobject_cast<QWebSocket*>(m_device);
        if (ws) {
            ws->close(QWebSocketProtocol::CloseCodeAbnormalDisconnection);
        }
    }
}

void CommunicationManager::cleanupDevice()
{
    if (m_device) {
        m_device->deleteLater();
        m_device = nullptr;
    }
    m_connectTimer->stop();
    m_closeTimer->stop();
    m_wsPingTimer->stop();
    m_state = Unconnected;
}

#include "QTcpSocketManager.h"
#include <QTcpSocket>
#include <QTimer>

QTcpSocketManager::QTcpSocketManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<TcpConfig>("TcpConfig");
    qRegisterMetaType<QTcpSocketManager::State>("QTcpSocketManager::State");
    qRegisterMetaType<QTcpSocketManager::Error>("QTcpSocketManager::Error");

    m_socket = nullptr;
    m_recvTimer = new QTimer(this);
    m_sendTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_connectTimer = new QTimer(this);

    m_recvTimer->setSingleShot(true);
    m_recvTimer->setInterval(0);
    m_sendTimer->setSingleShot(true);
    m_sendTimer->setInterval(0);
    m_reconnectTimer->setSingleShot(true);
    m_connectTimer->setSingleShot(true);

    recreateSocket();

    connect(m_recvTimer, &QTimer::timeout, this, &QTcpSocketManager::processRecv);
    connect(m_sendTimer, &QTimer::timeout, this, &QTcpSocketManager::processSendQueue);
    connect(m_reconnectTimer, &QTimer::timeout, this, &QTcpSocketManager::doReconnect);
    connect(m_connectTimer, &QTimer::timeout, this, &QTcpSocketManager::onConnectTimeout);
}

QTcpSocketManager::~QTcpSocketManager()
{
    stop();
}

void QTcpSocketManager::start(const QHostAddress &host, quint16 port)
{
    TcpConfig config;
    config.remoteAddress = host;
    config.remoteHost = host.toString();
    config.remotePort = port;
    start(config);
}

void QTcpSocketManager::start(const TcpConfig &config)
{
    if (m_state != Stopped) {
        stop();
    }
    applyConfig(config);
    m_sendQueue.clear();
    setState(Connecting);
    doReconnect();
}

void QTcpSocketManager::stop()
{
    m_reconnectTimer->stop();
    m_recvTimer->stop();
    m_sendTimer->stop();
    m_connectTimer->stop();
    m_sendQueue.clear();
    setState(Stopped);
    if (m_socket) {
        // 先断开信号，避免 abort 触发 disconnected/error 再次 schedule 重连。
        QObject::disconnect(m_socket, nullptr, this, nullptr);
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->abort();
    }
    m_reconnectTimer->stop();
}

void QTcpSocketManager::send(const QByteArray &data)
{
    if (data.isEmpty()) return;

    m_sendQueue.enqueue(data);
    while (m_sendQueue.size() > m_config.maxQueueSize) {
        m_sendQueue.dequeue();
    }

    if (m_state == Connected && !m_sendTimer->isActive()) {
        m_sendTimer->start();
    }
}

void QTcpSocketManager::disconnectFromHost()
{
    if (m_state == Connected && m_socket) {
        m_socket->disconnectFromHost();
    }
}

QTcpSocketManager::State QTcpSocketManager::state() const { return m_state; }
bool QTcpSocketManager::isConnected() const { return m_state == Connected && m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
QHostAddress QTcpSocketManager::remoteAddress() const { return m_config.remoteAddress; }
quint16 QTcpSocketManager::remotePort() const { return m_config.remotePort; }
TcpConfig QTcpSocketManager::currentConfig() const { return m_config; }

void QTcpSocketManager::onReadyRead()
{
    if (!m_recvTimer->isActive()) {
        m_recvTimer->start();
    }
}

void QTcpSocketManager::onSocketError(QAbstractSocket::SocketError err)
{
    if (m_state == Stopped || !m_socket) return;

    Error code = mapQtSocketError(err);
    emit errorOccurred(code, m_socket->errorString());

    m_connectTimer->stop();
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        setState(Reconnecting);
        m_socket->abort();
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(m_config.reconnectMs);
        }
    }
}

void QTcpSocketManager::onConnected()
{
    m_connectTimer->stop();
    m_reconnectTimer->stop();
    setState(Connected);
    emit connected();

    if (!m_sendQueue.isEmpty() && !m_sendTimer->isActive()) {
        m_sendTimer->start();
    }
}

void QTcpSocketManager::onDisconnected()
{
    m_connectTimer->stop();
    emit disconnected();

    if (m_state != Stopped) {
        setState(Reconnecting);
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(m_config.reconnectMs);
        }
    }
}

void QTcpSocketManager::processRecv()
{
    if (!m_socket) return;
    while (m_socket->bytesAvailable() > 0) {
        QByteArray data = m_socket->readAll();
        if (!data.isEmpty()) {
            emit dataReceived(data);
        }
    }
}

void QTcpSocketManager::processSendQueue()
{
    if (m_state != Connected || !m_socket) return;

    while (!m_sendQueue.isEmpty()) {
        QByteArray data = m_sendQueue.dequeue();
        const qint64 written = m_socket->write(data);
        if (written < 0) {
            m_sendQueue.prepend(data);
            break;
        }
        if (written < data.size()) {
            m_sendQueue.prepend(data.mid(static_cast<int>(written)));
            break;
        }
    }
}

void QTcpSocketManager::onConnectTimeout()
{
    if (m_state != Connecting || !m_socket) return;

    emit errorOccurred(ConnectionTimeout, "Connection timed out");
    m_socket->abort();
    setState(Reconnecting);
    m_reconnectTimer->start(m_config.reconnectMs);
}

void QTcpSocketManager::recreateSocket()
{
    if (m_socket) {
        QObject::disconnect(m_socket, nullptr, this, nullptr);
        m_socket->abort();
        delete m_socket;
        m_socket = nullptr;
    }

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &QTcpSocketManager::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &QTcpSocketManager::onSocketError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &QTcpSocketManager::onSocketError);
#endif
    connect(m_socket, &QTcpSocket::connected, this, &QTcpSocketManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &QTcpSocketManager::onDisconnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, [this](qint64) {
        if (!m_sendQueue.isEmpty() && m_state == Connected && !m_sendTimer->isActive()) {
            m_sendTimer->start();
        }
    });
}

bool QTcpSocketManager::shouldBindLocal() const
{
    const QHostAddress &addr = m_config.bindAddress;
    if (addr.isNull() || addr == QHostAddress::Any || addr == QHostAddress::AnyIPv4
            || addr == QHostAddress::AnyIPv6 || addr == QHostAddress::Broadcast) {
        return false;
    }
    return true;
}

void QTcpSocketManager::doReconnect()
{
    if (m_state == Stopped) return;

    // abort() 之后同一 QTcpSocket 经常还不是 UnconnectedState，再 bind 会刷警告。
    recreateSocket();

    if (shouldBindLocal()) {
        if (!m_socket->bind(m_config.bindAddress, 0)) {
            emit errorOccurred(BindError, m_socket->errorString());
            setState(Reconnecting);
            m_reconnectTimer->start(m_config.reconnectMs);
            return;
        }
    }

    m_socket->connectToHost(
        m_config.remoteHost.isEmpty() ? m_config.remoteAddress.toString() : m_config.remoteHost,
        m_config.remotePort);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, m_config.keepAlive ? 1 : 0);
    m_connectTimer->start(m_config.connectTimeoutMs);
}

void QTcpSocketManager::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

QTcpSocketManager::Error QTcpSocketManager::mapQtSocketError(QAbstractSocket::SocketError err)
{
    switch (err) {
        case QAbstractSocket::ConnectionRefusedError:    return ConnectionRefused;
        case QAbstractSocket::HostNotFoundError:         return HostNotFound;
        case QAbstractSocket::SocketAccessError:         return PermissionDenied;
        case QAbstractSocket::NetworkError:              return NetworkError;
        case QAbstractSocket::RemoteHostClosedError:     return RemoteClosed;
        default:                                         return NetworkError;
    }
}

void QTcpSocketManager::applyConfig(const TcpConfig &config)
{
    m_config = config;
    if (m_config.remoteHost.isEmpty())
        m_config.remoteHost = m_config.remoteAddress.toString();
    if (m_config.bindAddress.isNull()) {
        m_config.bindAddress = QHostAddress::Any;
    }
    m_config.connectTimeoutMs = qMax(1000, m_config.connectTimeoutMs);
    m_config.reconnectMs = qMax(1000, m_config.reconnectMs);
    m_config.maxQueueSize = qMax(16, m_config.maxQueueSize);
}

#if 0
// 创建实例
m_tcp = new QTcpSocketManager(this);

// 停止按钮直接连槽
connect(ui->btnStop, &QPushButton::clicked, m_tcp, &QTcpSocketManager::stop);

// 启动连接
connect(ui->btnConnect, &QPushButton::clicked, [=]() {
    TcpConfig cfg;
    cfg.remoteAddress = QHostAddress(ui->editIp->text());
    cfg.remotePort = ui->spinPort->value();
    cfg.reconnectMs = 2000;
    m_tcp->start(cfg);
});

// 收数据
connect(m_tcp, &QTcpSocketManager::dataReceived, [=](const QByteArray &data) {
    ui->logEdit->appendPlainText(QString("[RX] %1").arg(QString(data)));
});

// 状态更新
connect(m_tcp, &QTcpSocketManager::stateChanged, [=](QTcpSocketManager::State s) {
    QString text;
    switch(s) {
        case QTcpSocketManager::Connected: text = "已连接"; break;
        case QTcpSocketManager::Connecting: text = "连接中..."; break;
        case QTcpSocketManager::Reconnecting: text = "断线重连中..."; break;
        case QTcpSocketManager::Stopped: text = "已断开"; break;
    }
    ui->statusLabel->setText(text);
});

// 发数据
connect(ui->btnSend, &QPushButton::clicked, [=]() {
    m_tcp->send(ui->sendEdit->text().toUtf8());
});
#endif

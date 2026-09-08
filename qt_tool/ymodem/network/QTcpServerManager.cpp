#include "QTcpServerManager.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>

QTcpServerManager::QTcpServerManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<TcpServerConfig>("TcpServerConfig");
    qRegisterMetaType<TcpClientInfo>("TcpClientInfo");
    qRegisterMetaType<ClientConnId>("ClientConnId");
    qRegisterMetaType<QTcpServerManager::State>("QTcpServerManager::State");
    qRegisterMetaType<QTcpServerManager::Error>("QTcpServerManager::Error");

    m_server = new QTcpServer(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    connect(m_server, &QTcpServer::newConnection, this, &QTcpServerManager::onNewConnection);
    connect(m_server, &QTcpServer::acceptError, this, [=](QAbstractSocket::SocketError) {
        emit errorOccurred(AcceptError, m_server->errorString());
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, &QTcpServerManager::doReconnect);
}

QTcpServerManager::~QTcpServerManager()
{
    stop();
}

void QTcpServerManager::start(quint16 listenPort)
{
    TcpServerConfig config;
    config.listenPort = listenPort;
    start(config);
}

void QTcpServerManager::start(const TcpServerConfig &config)
{
    if (m_state != Stopped) {
        stop();
    }
    applyConfig(config);
    setState(Starting);
    doReconnect();
}

void QTcpServerManager::stop()
{
    m_reconnectTimer->stop();

    // 断开所有客户端
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket *socket = it.value();
        TcpClientInfo info = getClientInfo(socket);
        socket->abort();
        socket->deleteLater();
        emit clientDisconnected(info);
    }
    m_clients.clear();

    if (m_server->isListening()) {
        m_server->close();
    }
    setState(Stopped);
}

void QTcpServerManager::sendToClient(ClientConnId connId, const QByteArray &data)
{
    if (data.isEmpty() || !m_clients.contains(connId)) return;
    QTcpSocket *socket = m_clients[connId];
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(data);
    }
}

void QTcpServerManager::broadcast(const QByteArray &data)
{
    if (data.isEmpty()) return;
    for (QTcpSocket *socket : m_clients) {
        if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(data);
        }
    }
}

void QTcpServerManager::disconnectClient(ClientConnId connId)
{
    if (!m_clients.contains(connId)) return;
    QTcpSocket *socket = m_clients[connId];
    socket->disconnectFromHost();
}

QList<TcpClientInfo> QTcpServerManager::clients() const
{
    QList<TcpClientInfo> list;
    for (QTcpSocket *socket : m_clients) {
        list.append(getClientInfo(socket));
    }
    return list;
}

QTcpServerManager::State QTcpServerManager::state() const { return m_state; }
bool QTcpServerManager::isListening() const { return m_state == Listening && m_server->isListening(); }
quint16 QTcpServerManager::listenPort() const { return m_config.listenPort; }
TcpServerConfig QTcpServerManager::currentConfig() const { return m_config; }

void QTcpServerManager::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;

        socket->setParent(this);
        // 超过最大连接数直接断开
        if (m_clients.size() >= m_config.maxConnections) {
            socket->abort();
            socket->deleteLater();
            emit errorOccurred(MaxConnectionsReached, "Max connections reached, rejected new client");
            continue;
        }

        // 配置socket并记录连接时间
        socket->setSocketOption(QAbstractSocket::KeepAliveOption, m_config.keepAlive ? 1 : 0);
        socket->setProperty("connectTime", QDateTime::currentMSecsSinceEpoch());
        ClientConnId id = reinterpret_cast<ClientConnId>(socket);
        m_clients.insert(id, socket);

        // 连接客户端信号
        connect(socket, &QTcpSocket::readyRead, this, &QTcpServerManager::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &QTcpServerManager::onClientDisconnected);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                this, &QTcpServerManager::onClientError);

        emit clientConnected(getClientInfo(socket));
    }
}

void QTcpServerManager::onClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    processClientRecv(socket);
}

void QTcpServerManager::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        TcpClientInfo info = getClientInfo(socket);
        removeClient(socket);
        emit clientDisconnected(info);
    }
}

void QTcpServerManager::onClientError(QAbstractSocket::SocketError)
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    emit errorOccurred(ClientError, socket->errorString());
    if (socket->state() != QAbstractSocket::ConnectedState) {
        TcpClientInfo info = getClientInfo(socket);
        removeClient(socket);
        emit clientDisconnected(info);
    }
}

void QTcpServerManager::doReconnect()
{
    if (m_state == Stopped) return;

    if (m_server->isListening()) {
        m_server->close();
    }

    bool ok = m_server->listen(m_config.listenAddress, m_config.listenPort);
    if (ok) {
        setState(Listening);
        m_reconnectTimer->stop();
    } else {
        emit errorOccurred(mapServerError(m_server->serverError()), m_server->errorString());
        setState(Reconnecting);
        m_reconnectTimer->start(m_config.reconnectMs);
    }
}

void QTcpServerManager::processClientRecv(QTcpSocket *socket)
{
    if (!socket || !m_clients.contains(reinterpret_cast<ClientConnId>(socket))) return;

    while (socket->bytesAvailable() > 0) {
        QByteArray data = socket->readAll();
        if (!data.isEmpty()) {
            ClientConnId id = reinterpret_cast<ClientConnId>(socket);
            emit clientDataReceived(id, data);
        }
    }
}

void QTcpServerManager::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

QTcpServerManager::Error QTcpServerManager::mapServerError(QAbstractSocket::SocketError err)
{
    switch (err) {
        case QAbstractSocket::AddressInUseError:    return PortInUse;
        case QAbstractSocket::SocketAccessError:    return PermissionDenied;
        default:                                    return ListenFailed;
    }
}

void QTcpServerManager::applyConfig(const TcpServerConfig &config)
{
    m_config = config;
    if (m_config.listenAddress.isNull()) {
        m_config.listenAddress = QHostAddress::Any;
    }
    m_config.maxConnections = qMax(1, m_config.maxConnections);
    m_config.reconnectMs = qMax(1000, m_config.reconnectMs);
}

TcpClientInfo QTcpServerManager::getClientInfo(QTcpSocket *socket) const
{
    TcpClientInfo info;
    if (!socket) return info;
    info.connId = reinterpret_cast<ClientConnId>(socket);
    info.peerAddress = socket->peerAddress();
    info.peerPort = socket->peerPort();
    info.connectTime = socket->property("connectTime").toLongLong();
    return info;
}

void QTcpServerManager::removeClient(QTcpSocket *socket)
{
    if (!socket) return;
    ClientConnId id = reinterpret_cast<ClientConnId>(socket);
    if (m_clients.contains(id)) {
        m_clients.remove(id);
        socket->deleteLater();
    }
}
#if 0

// 创建实例
m_tcpServer = new QTcpServerManager(this);

// 停止按钮直接连槽
connect(ui->btnStop, &QPushButton::clicked, m_tcpServer, &QTcpServerManager::stop);

// 启动服务器
connect(ui->btnStart, &QPushButton::clicked, [=]() {
    TcpServerConfig cfg;
    cfg.listenPort = ui->spinPort->value();
    cfg.maxConnections = 16;
    m_tcpServer->start(cfg);
});

// 新客户端连接
connect(m_tcpServer, &QTcpServerManager::clientConnected, [=](const TcpClientInfo &client) {
    ui->logEdit->appendPlainText(QString("[新连接] %1:%2  ID:%3")
        .arg(client.peerAddress.toString()).arg(client.peerPort).arg((quint64)client.connId));
    ui->clientList->addItem(QString("%1:%2").arg(client.peerAddress.toString()).arg(client.peerPort));
});

// 客户端断开
connect(m_tcpServer, &QTcpServerManager::clientDisconnected, [=](const TcpClientInfo &client) {
    ui->logEdit->appendPlainText(QString("[断开] %1:%2").arg(client.peerAddress.toString()).arg(client.peerPort));
    // 刷新客户端列表
});

// 收到客户端数据
connect(m_tcpServer, &QTcpServerManager::clientDataReceived, [=](ClientConnId id, const QByteArray &data) {
    ui->logEdit->appendPlainText(QString("[RX][ID:%1] %2").arg((quint64)id).arg(QString(data)));
    // 回显给客户端
    m_tcpServer->sendToClient(id, "OK");
});

// 广播按钮
connect(ui->btnBroadcast, &QPushButton::clicked, [=]() {
    m_tcpServer->broadcast(ui->sendEdit->text().toUtf8());
});
#endif

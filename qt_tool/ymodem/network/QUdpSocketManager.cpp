#include "QUdpSocketManager.h"
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QAbstractSocket>
#include <QDebug>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <mswsock.h>
#endif

QUdpSocketManager::QUdpSocketManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<UdpConfig>("UdpConfig");
    qRegisterMetaType<UdpDatagram>("UdpDatagram");
    qRegisterMetaType<RemoteHost>("RemoteHost");
    qRegisterMetaType<QUdpSocketManager::State>("QUdpSocketManager::State");
    qRegisterMetaType<QUdpSocketManager::Error>("QUdpSocketManager::Error");

    m_socket = new QUdpSocket(this);
//    m_recvTimer = new QTimer(this);
    m_sendTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_hostCleanupTimer = new QTimer(this);

//    m_recvTimer->setSingleShot(true);
//    m_recvTimer->setInterval(0);
    m_sendTimer->setSingleShot(true);
    m_sendTimer->setInterval(0);
    m_reconnectTimer->setSingleShot(true);
    m_hostCleanupTimer->setInterval(60000);

    connect(m_socket, &QUdpSocket::readyRead, this, &QUdpSocketManager::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &QUdpSocketManager::onSocketError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &QUdpSocketManager::onSocketError);
#endif
//    connect(m_recvTimer, &QTimer::timeout, this, &QUdpSocketManager::processRecv);
    connect(m_sendTimer, &QTimer::timeout, this, &QUdpSocketManager::processSendQueue);
    connect(m_reconnectTimer, &QTimer::timeout, this, &QUdpSocketManager::doReconnect);
    connect(m_hostCleanupTimer, &QTimer::timeout, this, &QUdpSocketManager::cleanupStaleHosts);
}

QUdpSocketManager::~QUdpSocketManager()
{
    stop();
}

// 便捷接口：只传端口启动
void QUdpSocketManager::start(quint16 listenPort)
{
    UdpConfig config;
    config.listenPort = listenPort;
    start(config);
}

// 标准启动槽：传配置启动/重启
void QUdpSocketManager::start(const UdpConfig &config)
{
    if (m_state != Stopped) {
        stop();
    }
    applyConfig(config);
    m_sendQueue.clear();
    setState(Starting);
    m_hostCleanupTimer->start();
    doReconnect();
}

void QUdpSocketManager::stop()
{
    m_reconnectTimer->stop();
//    m_recvTimer->stop();
    m_sendTimer->stop();
    m_hostCleanupTimer->stop();
    m_sendQueue.clear();
    // 先标 Stopped，避免 close/abort 触发的 error 又把重连定时器拉起来。
    setState(Stopped);
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

void QUdpSocketManager::sendTo(const QByteArray &data, const QHostAddress &host, quint16 port)
{
    UdpDatagram dg;
    dg.data = data;
    dg.host = host;
    dg.port = port;
    dg.retryCount = 0;
    m_sendQueue.enqueue(dg);

    while (m_sendQueue.size() > m_config.maxQueueSize) {
        m_sendQueue.dequeue();
    }

    if (m_state == Running && !m_sendTimer->isActive()) {
        m_sendTimer->start();
    }
}

void QUdpSocketManager::sendToAll(const QByteArray &data)
{
    for (const RemoteHost &host : m_remoteHosts) {
        sendTo(data, host.address, host.port);
    }
}

void QUdpSocketManager::broadcast(const QByteArray &data, quint16 targetPort)
{
    sendTo(data, QHostAddress::Broadcast, targetPort);
}

void QUdpSocketManager::reply(const QByteArray &data, const UdpDatagram &recvDatagram)
{
    if (recvDatagram.host.isNull() || recvDatagram.port == 0) return;
    sendTo(data, recvDatagram.host, recvDatagram.port);
}

void QUdpSocketManager::addRemoteHost(const QHostAddress &host, quint16 port)
{
    if(m_remoteHosts.size()>256)return;
    RemoteHost h;
    h.address = host;
    h.host = host.toString();
    h.port = port;
    h.lastSeen = QDateTime::currentMSecsSinceEpoch();

    int idx = m_remoteHosts.indexOf(h);
    if (idx < 0) {
        m_remoteHosts.enqueue(h);
        emit remoteHostAdded(h);
        emit remoteHostListChanged();

    } else {
        m_remoteHosts[idx].lastSeen = h.lastSeen;
    }
}

void QUdpSocketManager::removeRemoteHost(const QHostAddress &host, quint16 port)
{
    RemoteHost h;
    h.address = host;
    h.host = host.toString();
    h.port = port;
    int idx = m_remoteHosts.indexOf(h);
    if (idx >= 0) {
        RemoteHost removed = m_remoteHosts.takeAt(idx);
        emit remoteHostRemoved(removed);
        emit remoteHostListChanged();
    }
}

void QUdpSocketManager::clearRemoteHosts()
{
    if (!m_remoteHosts.isEmpty()) {
        m_remoteHosts.clear();
        emit remoteHostListChanged();
    }
}

QList<RemoteHost> QUdpSocketManager::remoteHosts() const { return m_remoteHosts; }
QUdpSocketManager::State QUdpSocketManager::state() const { return m_state; }
bool QUdpSocketManager::isRunning() const { return m_state == Running && m_socket->state() == QAbstractSocket::BoundState; }
quint16 QUdpSocketManager::listenPort() const { return m_config.listenPort; }
UdpConfig QUdpSocketManager::currentConfig() const { return m_config; }

void QUdpSocketManager::onReadyRead()
{
//     qDebug() << "触发readyRead信号";
//    if (!m_recvTimer->isActive())
//     m_recvTimer->start();
     processRecv();
}

void QUdpSocketManager::onSocketError(QAbstractSocket::SocketError err)
{
    if (m_state == Stopped) return;

    Error code = mapQtSocketError(err);
    emit errorOccurred(code, m_socket->errorString());

    if (m_state != Stopped && m_socket->state() != QAbstractSocket::BoundState) {
        setState(Reconnecting);
        m_socket->abort();
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(m_config.reconnectMs);
        }
    }
}

void QUdpSocketManager::processRecv()
{

    while (m_socket->hasPendingDatagrams()) {
        qint64 size = m_socket->pendingDatagramSize();
//        qDebug() << "进入processRecv，当前待处理包大小:" << m_socket->pendingDatagramSize() << "最大允许包长:" << m_config.maxPacketSize;
        if (size <= 0) break;

        if (size > m_config.maxPacketSize) {
            QByteArray discard;
            discard.resize(static_cast<int>(size));
            m_socket->readDatagram(discard.data(), size);
            continue;
        }

        UdpDatagram dg;
        dg.data.resize(static_cast<int>(size));//防崩溃
        qint64 readLen = m_socket->readDatagram(dg.data.data(), size, &dg.host, &dg.port);

        if (readLen > 0) {
            dg.data.resize(static_cast<int>(readLen));//防脏数据。
            dg.timestamp = QDateTime::currentMSecsSinceEpoch();
            updateRemoteHost(dg.host, dg.port);
            emit datagramReceived(dg);
        }

//        m_socket->readAll();
//        qDebug()<<"udp recive";
    }
    // 兜底：读完再检查一次，防止极端情况还有残留包没读，避免卡住
//    if (m_socket->hasPendingDatagrams()) {
//        m_recvTimer->start();
//    }
}

void QUdpSocketManager::processSendQueue()
{
    if (m_state != Running) return;

    while (!m_sendQueue.isEmpty()) {
        UdpDatagram dg = m_sendQueue.dequeue();
        qint64 written = m_socket->writeDatagram(dg.data, dg.host, dg.port);

        if (written <= 0) {
            dg.retryCount++;
            if (dg.retryCount > m_maxRetry) {
                emit errorOccurred(SendFailed, m_socket->errorString());
            } else {
                m_sendQueue.enqueue(dg);
            }
            break;
        }
    }

    if (!m_sendQueue.isEmpty() && m_state == Running) {
        m_sendTimer->start();
    }
}

void QUdpSocketManager::doReconnect()
{
    if (m_state == Stopped) return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;

    bool ok = m_socket->bind(m_config.bindAddress, m_config.listenPort);
#ifdef Q_OS_WIN
    // 仅Windows下生效，其他平台直接跳过，完全不影响跨平台性
    BOOL disableConnReset = FALSE;
    DWORD bytesReturned = 0;
    WSAIoctl(m_socket->socketDescriptor(), SIO_UDP_CONNRESET,
             &disableConnReset, sizeof(disableConnReset),
             nullptr, 0, &bytesReturned, nullptr, nullptr);
#endif

    if (ok) {
        setState(Running);
        m_reconnectTimer->stop();
        if (!m_sendQueue.isEmpty() && !m_sendTimer->isActive()) {
            m_sendTimer->start();
        }
    } else {
        emit errorOccurred(mapQtSocketError(m_socket->error()), m_socket->errorString());
        m_reconnectTimer->start(m_config.reconnectMs);
    }
}

void QUdpSocketManager::cleanupStaleHosts()
{
    if (m_remoteHosts.isEmpty()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 timeoutMs = static_cast<qint64>(m_config.hostTimeoutSec) * 1000;
    bool changed = false;

    for (int i = m_remoteHosts.size() - 1; i >= 0; --i) {
        if (now - m_remoteHosts[i].lastSeen > timeoutMs) {
            RemoteHost removed = m_remoteHosts.takeAt(i);
            emit remoteHostRemoved(removed);
            changed = true;
        }
    }

    if (changed) emit remoteHostListChanged();
}

void QUdpSocketManager::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

void QUdpSocketManager::updateRemoteHost(const QHostAddress &host, quint16 port)
{
    if (host.isNull() || host == QHostAddress::Broadcast || host == QHostAddress::Any) return;

    RemoteHost h;
    h.address = host;
    h.host = host.toString();
    h.port = port;

    int idx = m_remoteHosts.indexOf(h);
    if (idx >= 0) {
        m_remoteHosts[idx].lastSeen = QDateTime::currentMSecsSinceEpoch();
    } else {
        h.lastSeen = QDateTime::currentMSecsSinceEpoch();
//        m_remoteHosts.append(h);
        addRemoteHost(h.address, h.port);//添加ip地址，超过256个地址就不添加了
//        emit remoteHostAdded(h);
//        emit remoteHostListChanged();
    }
}

QUdpSocketManager::Error QUdpSocketManager::mapQtSocketError(QAbstractSocket::SocketError err)
{
    switch (err) {
        case QAbstractSocket::AddressInUseError:        return PortInUse;
        case QAbstractSocket::SocketAccessError:        return PermissionDenied;
        case QAbstractSocket::NetworkError:             return NetworkUnreachable;
        default:                                        return SocketError;
    }
}

void QUdpSocketManager::applyConfig(const UdpConfig &config)
{
    m_config = config;
    // 空地址自动fallback到所有网卡
    if (m_config.bindAddress.isNull()) {
        m_config.bindAddress = QHostAddress::Any;
    }
    // 参数边界检查
    m_config.reconnectMs = qMax(1000, m_config.reconnectMs);
    m_config.maxPacketSize = qBound(512, m_config.maxPacketSize, 65535);
    m_config.maxQueueSize = qMax(16, m_config.maxQueueSize);
    m_config.hostTimeoutSec = qMax(60, m_config.hostTimeoutSec);
}
#if 0
// 1. 创建实例
m_udp = new QUdpSocketManager(this);

// 2. 停止按钮直接连槽，不用写lambda
connect(ui->btnStop, &QPushButton::clicked, m_udp, &QUdpSocketManager::stop);

// 3. 启动按钮拿UI参数构造配置，传进去启动
connect(ui->btnStart, &QPushButton::clicked, [=]() {
    UdpConfig config;
    config.listenPort = ui->spinPort->value();
    config.reconnectMs = ui->spinReconnect->value();
    config.hostTimeoutSec = ui->spinTimeout->value();
    // 其他参数不填用默认值
    m_udp->start(config);
});

// 4. 改参数重启：直接调start传新配置，自动重启
// 比如用户点了"应用设置"按钮：
connect(ui->btnApply, &QPushButton::clicked, [=]() {
    UdpConfig newConfig = m_udp->currentConfig();
    newConfig.listenPort = ui->spinPort->value();
    newConfig.reconnectMs = ui->spinReconnect->value();
    m_udp->start(newConfig); // 自动停了再启，新参数生效
});

// 其他信号连接和之前一样
connect(m_udp, &QUdpSocketManager::datagramReceived, ...);
#endif

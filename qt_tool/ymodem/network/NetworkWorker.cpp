#include "NetworkWorker.h"

#include <QHostInfo>

NetworkWorker::NetworkWorker(QObject *parent)
    : IoSource(parent)
{
}

NetworkWorker::~NetworkWorker()
{
    cleanupCurrentNet();
}

void NetworkWorker::slotOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort)
{
    cleanupCurrentNet();
    m_currentProto = proto;
    m_bindAddress = QHostAddress(localIp);
    m_bindPort = localPort;
    QHostAddress bindAddr = m_bindAddress;

    if (proto == NetProtocol::TcpServer) {
        m_tcpServer = new QTcpServerManager(this);
        TcpServerConfig cfg;
        cfg.listenAddress = bindAddr;
        cfg.listenPort = localPort;

        connect(m_tcpServer, &QTcpServerManager::clientConnected, this, &NetworkWorker::onTcpServerClientConnected);
        connect(m_tcpServer, &QTcpServerManager::clientDisconnected, this, &NetworkWorker::onTcpServerClientDisconnected);
        connect(m_tcpServer, &QTcpServerManager::clientDataReceived, this, &NetworkWorker::onTcpServerData);
        connect(m_tcpServer, &QTcpServerManager::errorOccurred, this, &NetworkWorker::onTcpServerError);
        connect(m_tcpServer, &QTcpServerManager::stateChanged, this, &NetworkWorker::onTcpServerState);

        m_tcpServer->start(cfg);
        emit sigClientCount(0);
    }
    else if (proto == NetProtocol::TcpClient) {
        // TCP客户端对象复用，打开时创建，关闭/切换协议才销毁，连接/断开只调用start/stop
        m_tcpClient = new QTcpSocketManager(this);
        TcpConfig cfg;
        cfg.bindAddress = bindAddr;
        cfg.bindPort = localPort;

        connect(m_tcpClient, &QTcpSocketManager::connected, this, &NetworkWorker::onTcpClientConnected);
        connect(m_tcpClient, &QTcpSocketManager::disconnected, this, &NetworkWorker::onTcpClientDisconnected);
        connect(m_tcpClient, &QTcpSocketManager::dataReceived, this, &NetworkWorker::onTcpClientData);
        connect(m_tcpClient, &QTcpSocketManager::errorOccurred, this, &NetworkWorker::onTcpClientError);
        connect(m_tcpClient, &QTcpSocketManager::stateChanged, this, &NetworkWorker::onTcpClientState);
    }
    else if (proto == NetProtocol::Udp) {
        m_udp = new QUdpSocketManager(this);
        UdpConfig cfg;
        cfg.bindAddress = bindAddr;
        cfg.listenPort = localPort;

        connect(m_udp, &QUdpSocketManager::datagramReceived, this, &NetworkWorker::onUdpDatagram);
        connect(m_udp, &QUdpSocketManager::errorOccurred, this, &NetworkWorker::onUdpError);
        connect(m_udp, &QUdpSocketManager::stateChanged, this, &NetworkWorker::onUdpState);
        connect(m_udp, &QUdpSocketManager::remoteHostAdded, this, &NetworkWorker::onUdpHostAdded);
        connect(m_udp, &QUdpSocketManager::remoteHostRemoved, this, &NetworkWorker::onUdpHostRemoved);

        m_udp->start(cfg);
        emit sigStateText("UDP监听中");
    }
}

void NetworkWorker::slotCloseNetwork()
{
    cleanupCurrentNet();
    m_currentProto = static_cast<NetProtocol>(-1);
    emit sigStateText("已关闭");
    emit sigClientCount(0);
}

void NetworkWorker::slotTcpConnect(QString remoteIp, quint16 remotePort)
{
    if (m_currentProto != NetProtocol::TcpClient || !m_tcpClient) return;
    TcpConfig cfg = m_tcpClient->currentConfig();
    cfg.remoteHost = remoteIp.trimmed();
    cfg.remoteAddress = QHostAddress(cfg.remoteHost);
    cfg.remotePort = remotePort;
    cfg.bindAddress = m_bindAddress;
    cfg.bindPort = m_bindPort;
    m_tcpClient->start(cfg);
}

void NetworkWorker::slotTcpDisconnect()
{
    if (m_currentProto != NetProtocol::TcpClient || !m_tcpClient) return;
    // 仅断开连接，不销毁对象，下次连接直接复用
    m_tcpClient->stop();
}

void NetworkWorker::slotSendData(QByteArray data, QString remoteIp, quint16 remotePort)
{
    if (data.isEmpty())
        return;

    if (m_currentProto == NetProtocol::TcpServer && m_tcpServer) {
        if (remoteIp.isEmpty() || remotePort == 0)
            m_tcpServer->broadcast(data);
        else
            sendToTcpClient(data, remoteIp, remotePort);
    } else if (m_currentProto == NetProtocol::TcpClient && m_tcpClient) {
        m_tcpClient->send(data);
    } else if (m_currentProto == NetProtocol::Udp && m_udp) {
        if (remoteIp.isEmpty()) {
            m_udp->broadcast(data, remotePort);
        } else {
            QHostAddress addr(remoteIp);
            if (addr.isNull()) {
                const QHostInfo info = QHostInfo::fromName(remoteIp);
                if (info.addresses().isEmpty()) {
                    emit sigError(QStringLiteral("无法解析主机 %1").arg(remoteIp));
                    return;
                }
                addr = info.addresses().first();
                for (const QHostAddress &item : info.addresses()) {
                    if (item.protocol() == QAbstractSocket::IPv4Protocol) {
                        addr = item;
                        break;
                    }
                }
            }
            m_udp->sendTo(data, addr, remotePort);
        }
    }
}

void NetworkWorker::forwardPayload(IoPacket::Channel channel, const QByteArray &data,
                                  const QString &peer, quint16 port)
{
    emit sigRecvData(data, peer, port);
    emitIoData(IoPacket::fromNetwork(channel, data, peer, port));
}

void NetworkWorker::sendToTcpClient(const QByteArray &data, const QString &remoteIp, quint16 remotePort)
{
    for (const TcpClientInfo &info : m_tcpServer->clients()) {
        if (info.peerAddress.toString() == remoteIp && info.peerPort == remotePort) {
            m_tcpServer->sendToClient(info.connId, data);
            return;
        }
    }
    emit sigError(QStringLiteral("未找到客户端 %1:%2").arg(remoteIp).arg(remotePort));
}

void NetworkWorker::cleanupCurrentNet()
{
    if (m_tcpServer) {
        m_tcpServer->blockSignals(true);
        disconnect(m_tcpServer, nullptr, this, nullptr);
        m_tcpServer->stop();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }
    if (m_tcpClient) {
        m_tcpClient->blockSignals(true);
        disconnect(m_tcpClient, nullptr, this, nullptr);
        m_tcpClient->stop();
        delete m_tcpClient;
        m_tcpClient = nullptr;
    }
    if (m_udp) {
        m_udp->blockSignals(true);
        disconnect(m_udp, nullptr, this, nullptr);
        m_udp->stop();
        delete m_udp;
        m_udp = nullptr;
    }
}

// ------------------------------ 信号转发（全部加空指针保护） ------------------------------
// TCP服务端
void NetworkWorker::onTcpServerClientConnected(const TcpClientInfo &client)
{
    if (!m_tcpServer) return; // 空指针保护，防止残留信号
    emit sigClientConnected(client.peerAddress.toString(), client.peerPort);
    emit sigClientCount(m_tcpServer->clients().size());
}

void NetworkWorker::onTcpServerClientDisconnected(const TcpClientInfo &client)
{
    if (!m_tcpServer) return;
    emit sigClientDisconnected(client.peerAddress.toString(), client.peerPort);
    emit sigClientCount(m_tcpServer->clients().size());
}

void NetworkWorker::onTcpServerData(ClientConnId id, const QByteArray &data)
{
    if (!m_tcpServer) return;
    for (const TcpClientInfo &info : m_tcpServer->clients()) {
        if (info.connId == id) {
            forwardPayload(IoPacket::TcpServer, data, info.peerAddress.toString(), info.peerPort);
            break;
        }
    }
}

void NetworkWorker::onTcpServerError(QTcpServerManager::Error err, const QString &errStr)
{
    Q_UNUSED(err)
    emit sigError(errStr);
}

void NetworkWorker::onTcpServerState(QTcpServerManager::State state)
{
    if (state == QTcpServerManager::Listening) emit sigStateText("TCP服务端监听中");
    else if (state == QTcpServerManager::Reconnecting) emit sigStateText("监听失败，重连中...");
}

// TCP客户端
void NetworkWorker::onTcpClientConnected()
{
    if (!m_tcpClient) return;
    emit sigTcpConnected();
}

void NetworkWorker::onTcpClientDisconnected()
{
    if (!m_tcpClient) return;
    emit sigTcpDisconnected();
}

void NetworkWorker::onTcpClientData(const QByteArray &data)
{
    if (!m_tcpClient) return;
    QString peer = m_tcpClient->currentConfig().remoteHost;
    if (peer.isEmpty())
        peer = m_tcpClient->remoteAddress().toString();
    forwardPayload(IoPacket::TcpClient, data, peer, m_tcpClient->remotePort());
}

void NetworkWorker::onTcpClientError(QTcpSocketManager::Error err, const QString &errStr)
{
    Q_UNUSED(err)
    emit sigError(errStr);
}

void NetworkWorker::onTcpClientState(QTcpSocketManager::State state)
{
    if (state == QTcpSocketManager::Connecting) emit sigStateText("连接中...");
    else if (state == QTcpSocketManager::Connected) emit sigStateText("已连接");
    else if (state == QTcpSocketManager::Reconnecting) emit sigStateText("重连中...");
}

// UDP
void NetworkWorker::onUdpDatagram(const UdpDatagram &dg)
{
    if (!m_udp) return;
    forwardPayload(IoPacket::Udp, dg.data, dg.host.toString(), dg.port);
}

void NetworkWorker::onUdpError(QUdpSocketManager::Error err, const QString &errStr)
{
    Q_UNUSED(err)
    emit sigError(errStr);
}

void NetworkWorker::onUdpState(QUdpSocketManager::State state)
{
    if (state == QUdpSocketManager::Running) emit sigStateText("UDP监听中");
    else if (state == QUdpSocketManager::Reconnecting) emit sigStateText("绑定失败，重连中...");
}

void NetworkWorker::onUdpHostAdded(const RemoteHost &host)
{
    if (!m_udp) return;
    emit sigClientConnected(host.address.toString(), host.port);
}

void NetworkWorker::onUdpHostRemoved(const RemoteHost &host)
{
    if (!m_udp) return;
    emit sigClientDisconnected(host.address.toString(), host.port);
}


#include "NetworkWorker.h"
#include <QDebug>

NetworkWorker::NetworkWorker(QObject *parent)
    : QObject(parent)
{
}

NetworkWorker::~NetworkWorker()
{
    // 析构时直接delete，线程即将退出事件循环停止，deleteLater不会执行
    cleanupCurrentNet(true);
}

void NetworkWorker::slotOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort)
{
    // 先彻底清理旧实例，再创建新的
    cleanupCurrentNet();
    m_currentProto = proto;
    QHostAddress bindAddr(localIp);

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
    TcpConfig cfg;
    cfg.remoteAddress = QHostAddress(remoteIp);
    cfg.remotePort = remotePort;
    m_tcpClient->start(cfg);
}

void NetworkWorker::slotTcpDisconnect()
{
    if (m_currentProto != NetProtocol::TcpClient || !m_tcpClient) return;
    // 仅断开连接，不销毁对象，下次连接直接复用
    m_tcpClient->stop();
}

void NetworkWorker::slotSendData(QByteArray data, QString remoteIp, quint16 remotePort)
{   qDebug()<<"send"<<remoteIp<<remotePort<<data;
    if (m_currentProto == NetProtocol::TcpServer && m_tcpServer) {
        m_tcpServer->broadcast(data);
    }
    else if (m_currentProto == NetProtocol::TcpClient && m_tcpClient) {
        m_tcpClient->send(data);
    }
    else if (m_currentProto == NetProtocol::Udp && m_udp) {
        QHostAddress remoteAddr(remoteIp);
        m_udp->sendTo(data, remoteAddr, remotePort);
        qDebug()<<"udp send data"<<remoteAddr<<remotePort;
    }
}

void NetworkWorker::cleanupCurrentNet(bool isDestructing)
{
    // 清理TCP服务端
    if (m_tcpServer) {
        m_tcpServer->blockSignals(true); // 阻塞所有信号，防止清理过程中发信号
        disconnect(m_tcpServer, nullptr, this, nullptr); // 断开所有和Worker的连接
        m_tcpServer->stop(); // 同步停止服务，断开所有客户端
        if (isDestructing) {
            delete m_tcpServer;
        } else {
            m_tcpServer->deleteLater(); // 运行时投递到事件循环安全删除
        }
        m_tcpServer = nullptr;
    }
    // 清理TCP客户端
    if (m_tcpClient) {
        m_tcpClient->blockSignals(true);
        disconnect(m_tcpClient, nullptr, this, nullptr);
        m_tcpClient->stop(); // 断开连接，释放socket
        if (isDestructing) {
            delete m_tcpClient;
        } else {
            m_tcpClient->deleteLater();
        }
        m_tcpClient = nullptr;
    }
    // 清理UDP
    if (m_udp) {
        m_udp->blockSignals(true);
        disconnect(m_udp, nullptr, this, nullptr);
        m_udp->stop(); // 关闭socket，清空主机列表
        if (isDestructing) {
            delete m_udp;
        } else {
            m_udp->deleteLater();
        }
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
            emit sigRecvData(data, info.peerAddress.toString(), info.peerPort);
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
    emit sigRecvData(data, m_tcpClient->remoteAddress().toString(), m_tcpClient->remotePort());
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
    emit sigRecvData(dg.data, dg.host.toString(), dg.port);
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

#if 0

#include <QApplication>
#include "NetAssistWidget.h"
#include "NetworkWorker.h"
#include <QThread>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qRegisterMetaType<NetProtocol>("NetProtocol");

    NetAssistWidget ui;

    QThread workerThread;
    NetworkWorker *worker = new NetworkWorker; // 无parent，手动管理生命周期
    worker->moveToThread(&workerThread);

    // 移除原来的finished→deleteLater连接，改为手动销毁
    // QObject::connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);

    // UI -> 网络线程
    QObject::connect(&ui, &NetAssistWidget::sigOpenNetwork, worker, &NetworkWorker::slotOpenNetwork);
    QObject::connect(&ui, &NetAssistWidget::sigCloseNetwork, worker, &NetworkWorker::slotCloseNetwork);
    QObject::connect(&ui, &NetAssistWidget::sigTcpConnect, worker, &NetworkWorker::slotTcpConnect);
    QObject::connect(&ui, &NetAssistWidget::sigTcpDisconnect, worker, &NetworkWorker::slotTcpDisconnect);
    QObject::connect(&ui, &NetAssistWidget::sigSendData, worker, &NetworkWorker::slotSendData);

    // 网络线程 -> UI
    QObject::connect(worker, &NetworkWorker::sigRecvData, &ui, &NetAssistWidget::slotRecvData);
    QObject::connect(worker, &NetworkWorker::sigClientConnected, &ui, &NetAssistWidget::slotClientConnected);
    QObject::connect(worker, &NetworkWorker::sigClientDisconnected, &ui, &NetAssistWidget::slotClientDisconnected);
    QObject::connect(worker, &NetworkWorker::sigTcpConnected, &ui, &NetAssistWidget::slotTcpConnected);
    QObject::connect(worker, &NetworkWorker::sigTcpDisconnected, &ui, &NetAssistWidget::slotTcpDisconnected);
    QObject::connect(worker, &NetworkWorker::sigError, &ui, &NetAssistWidget::slotError);
    QObject::connect(worker, &NetworkWorker::sigStateText, &ui, &NetAssistWidget::slotStateText);
    QObject::connect(worker, &NetworkWorker::sigClientCount, &ui, &NetAssistWidget::slotClientCount);

    workerThread.start();
    ui.show();

    int ret = a.exec();

    // 安全退出顺序：先停止线程事件循环，等待线程结束，再delete Worker
    workerThread.quit();
    workerThread.wait();
    delete worker; // 线程结束后直接删除，不依赖事件循环，100%释放内存

    return ret;
}
#endif

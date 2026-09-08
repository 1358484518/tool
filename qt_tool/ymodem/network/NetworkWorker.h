#ifndef NETWORKWORKER_H
#define NETWORKWORKER_H

#include <QObject>
#include "common/IoData.h"
#include "NetCommon.h"
#include "QUdpSocketManager.h"
#include "QTcpSocketManager.h"
#include "QTcpServerManager.h"

class NetworkWorker : public IoSource
{
    Q_OBJECT
public:
    explicit NetworkWorker(QObject *parent = nullptr);
    ~NetworkWorker() override;

public slots:
    void slotOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort);
    void slotCloseNetwork();
    void slotTcpConnect(QString remoteIp, quint16 remotePort);
    void slotTcpDisconnect();
    void slotSendData(QByteArray data, QString remoteIp, quint16 remotePort);

signals:
    void sigRecvData(QByteArray data, QString fromIp, quint16 fromPort);
    void sigClientConnected(QString ip, quint16 port);
    void sigClientDisconnected(QString ip, quint16 port);
    void sigTcpConnected();
    void sigTcpDisconnected();
    void sigError(QString errStr);
    void sigStateText(QString text);
    void sigClientCount(int count);

private slots:
    // TCP服务端
    void onTcpServerClientConnected(const TcpClientInfo &client);
    void onTcpServerClientDisconnected(const TcpClientInfo &client);
    void onTcpServerData(ClientConnId id, const QByteArray &data);
    void onTcpServerError(QTcpServerManager::Error err, const QString &errStr);
    void onTcpServerState(QTcpServerManager::State state);

    // TCP客户端
    void onTcpClientConnected();
    void onTcpClientDisconnected();
    void onTcpClientData(const QByteArray &data);
    void onTcpClientError(QTcpSocketManager::Error err, const QString &errStr);
    void onTcpClientState(QTcpSocketManager::State state);

    // UDP
    void onUdpDatagram(const UdpDatagram &dg);
    void onUdpError(QUdpSocketManager::Error err, const QString &errStr);
    void onUdpState(QUdpSocketManager::State state);
    void onUdpHostAdded(const RemoteHost &host);
    void onUdpHostRemoved(const RemoteHost &host);

private:
    // isDestructing: true=析构时直接delete，false=运行时用deleteLater投递到事件循环
    void cleanupCurrentNet(bool isDestructing = false);
    void sendToTcpClient(const QByteArray &data, const QString &remoteIp, quint16 remotePort);
    void forwardPayload(IoPacket::Channel channel, const QByteArray &data,
                        const QString &peer, quint16 port);

    NetProtocol m_currentProto = static_cast<NetProtocol>(-1);
    QHostAddress m_bindAddress = QHostAddress::Any;
    quint16 m_bindPort = 0;
    QUdpSocketManager  *m_udp = nullptr;
    QTcpSocketManager  *m_tcpClient = nullptr;
    QTcpServerManager  *m_tcpServer = nullptr;
};

#endif // NETWORKWORKER_H

#ifndef NETWORKWORKER_H
#define NETWORKWORKER_H

#include <QObject>
#include "common/IoData.h"
#include "NetCommon.h"
#include "QUdpSocketManager.h"
#include "QTcpSocketManager.h"
#include "QTcpServerManager.h"

/**
 * 网络后端，活在 NetAssistWidget 的 workerThread。
 * 同一时刻只保留一种协议对应的 Manager。
 * 收：IoSource::ioDataReceived；发：IoSource::sendIoData。
 */
class NetworkWorker : public IoSource
{
    Q_OBJECT
public:
    explicit NetworkWorker(QObject *parent = nullptr);  // 只登记类型，套接字在 open 时才建
    ~NetworkWorker() override;                          // 关掉当前协议的 Manager

public slots:
    void slotOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort);  // 按协议 bind/listen；TCP 客户端只记本机地址
    void slotCloseNetwork();                            // 关掉并删除当前 Manager
    void slotTcpConnect(QString remoteIp, quint16 remotePort);  // 仅 TCP 客户端：去连对端
    void slotTcpDisconnect();                           // 仅 TCP 客户端：主动断开
    void slotSendData(QByteArray data, QString remoteIp, quint16 remotePort);  // 组 IoPacket 后走 sendIoData

signals:
    void sigRecvData(QByteArray data, QString fromIp, quint16 fromPort);  // 原始收包，给旧 UI 用
    void sigClientConnected(QString ip, quint16 port);   // TCP 服务端新连接
    void sigClientDisconnected(QString ip, quint16 port); // TCP 服务端连接断开
    void sigTcpConnected();                             // TCP 客户端已连接
    void sigTcpDisconnected();                          // TCP 客户端已断开
    void sigError(QString errStr);                      // 把 Manager 错误转成字符串给 UI
    void sigStateText(QString text);                    // Listening / Connecting 等给人看的状态
    void sigClientCount(int count);                     // TCP 服务端当前连接数

private slots:
    void onTcpServerClientConnected(const TcpClientInfo &client);       // 转发新客户端并更新人数
    void onTcpServerClientDisconnected(const TcpClientInfo &client);    // 转发断开并更新人数
    void onTcpServerData(ClientConnId id, const QByteArray &data);      // 服务端收到某客户端数据
    void onTcpServerError(QTcpServerManager::Error err, const QString &errStr);  // 服务端错误转发给 UI
    void onTcpServerState(QTcpServerManager::State state);              // listen 状态变成文字

    void onTcpClientConnected();                        // 客户端连上，通知 UI
    void onTcpClientDisconnected();                     // 客户端断开，通知 UI
    void onTcpClientData(const QByteArray &data);       // 客户端收到对端数据
    void onTcpClientError(QTcpSocketManager::Error err, const QString &errStr);  // 客户端错误转发给 UI
    void onTcpClientState(QTcpSocketManager::State state);  // Connecting / Connected 变成文字

    void onUdpDatagram(const UdpDatagram &dg);          // UDP 收到一包，转成 IoPacket
    void onUdpError(QUdpSocketManager::Error err, const QString &errStr);  // UDP 错误转发给 UI
    void onUdpState(QUdpSocketManager::State state);    // bind 状态变成文字
    void onUdpHostAdded(const RemoteHost &host);        // 新见到的对端，让 UI 写入下拉
    void onUdpHostRemoved(const RemoteHost &host);      // 超时对端从列表拿掉

private:
    void cleanupCurrentNet();   // 关掉并删掉当前协议的 Manager
    void sendToTcpClient(const QByteArray &data, const QString &remoteIp, quint16 remotePort);  // 按 IP:端口找连接再发
    void forwardPayload(IoPacket::Channel channel, const QByteArray &data,
                        const QString &peer, quint16 port);  // 同时发 sigRecvData 和 ioDataReceived

protected:
    bool writeIoData(const IoPacket &packet) override;  // 按当前协议真正往套接字写

private:
    NetProtocol m_currentProto = static_cast<NetProtocol>(-1);
    QHostAddress m_bindAddress = QHostAddress::Any;
    quint16 m_bindPort = 0;     // TCP/UDP 监听口；TCP 客户端不拿它去 bind
    QUdpSocketManager  *m_udp = nullptr;
    QTcpSocketManager  *m_tcpClient = nullptr;
    QTcpServerManager  *m_tcpServer = nullptr;
};

#endif // NETWORKWORKER_H

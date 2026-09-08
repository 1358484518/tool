#ifndef QUDPSOCKETMANAGER_H
#define QUDPSOCKETMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QQueue>
#include <QList>
#include "NetCommon.h"

class QUdpSocket;
class QTimer;

struct UdpConfig
{
    QHostAddress bindAddress = QHostAddress::Any;
    quint16      listenPort = 0;
    int          reconnectMs = 3000;
    int          maxPacketSize = 65535;
    int          maxQueueSize = 128;
    int          hostTimeoutSec = 300;  // 对端多久没包就从列表拿掉
};
Q_DECLARE_METATYPE(UdpConfig)

struct UdpDatagram
{
    QByteArray    data;
    QHostAddress  host;
    quint16       port = 0;
    qint64        timestamp = 0;
    int           retryCount = 0;
};
Q_DECLARE_METATYPE(UdpDatagram)

/** UDP：bind 本地口、按对端发、记住最近见过的主机。 */
class QUdpSocketManager : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Stopped = 0,
        Starting,
        Running,
        Reconnecting
    };
    Q_ENUM(State)

    enum Error
    {
        NoError = 0,
        BindFailed,
        SendFailed,
        SocketError,
        PermissionDenied,
        PortInUse,
        NetworkUnreachable
    };
    Q_ENUM(Error)

    explicit QUdpSocketManager(QObject *parent = nullptr);  // 建发送/重连/对端超时三个定时器
    ~QUdpSocketManager() override;                          // stop 并释放套接字

    void start(quint16 listenPort);                         // 只指定本地端口，地址用 Any

    void sendTo(const QByteArray &data, const QHostAddress &host, quint16 port);  // 发给指定对端
    void sendToAll(const QByteArray &data);                 // 发给列表里记过的所有对端
    void broadcast(const QByteArray &data, quint16 targetPort);  // 255.255.255.255 广播
    void reply(const QByteArray &data, const UdpDatagram &recvDatagram);  // 回给刚才来包的地址

    void addRemoteHost(const QHostAddress &host, quint16 port);     // 手动记一个对端
    void removeRemoteHost(const QHostAddress &host, quint16 port);  // 从列表拿掉
    void clearRemoteHosts();                                // 清空已见过的对端
    QList<RemoteHost> remoteHosts() const;                  // 当前记住的对端列表

    State state() const;                                    // Starting / Running / Reconnecting
    bool isRunning() const;                                 // 是否已 bind 成功
    quint16 listenPort() const;                             // 当前本地端口
    UdpConfig currentConfig() const;                        // 拷贝一份 bind 参数

public slots:
    void start(const UdpConfig &config);                    // 按完整配置 bind
    void stop();                                            // 关套接字、停定时器、清空发送队列

signals:
    void datagramReceived(const UdpDatagram &datagram);     // 收到一包 UDP
    void stateChanged(QUdpSocketManager::State state);
    void errorOccurred(QUdpSocketManager::Error error, const QString &systemErrorString);
    void remoteHostAdded(const RemoteHost &host);           // 第一次见到这个对端
    void remoteHostRemoved(const RemoteHost &host);         // 超时或手动删掉
    void remoteHostListChanged();                           // 对端列表有增删

private slots:
    void onReadyRead();                                     // 有数据报立刻读出发出
    void onSocketError(QAbstractSocket::SocketError err);   // 映射错误并决定是否重新 bind
    void processRecv();                                     // 兼容旧定时读路径
    void processSendQueue();                                // 把排队的数据报尽量发出
    void doReconnect();                                     // bind 失败后按间隔再试
    void cleanupStaleHosts();                               // 把太久没来包的对端从列表拿掉

private:
    void setState(State newState);                          // 状态变了才发 stateChanged
    void updateRemoteHost(const QHostAddress &host, quint16 port);  // 刷新 lastSeen，没有则加入
    Error mapQtSocketError(QAbstractSocket::SocketError err);
    void applyConfig(const UdpConfig &config);              // 保存 bind 地址、端口、超时

    QUdpSocket *m_socket;
//    QTimer     *m_recvTimer;
    QTimer     *m_sendTimer;
    QTimer     *m_reconnectTimer;
    QTimer     *m_hostCleanupTimer;

    QQueue<UdpDatagram> m_sendQueue;
    QQueue<RemoteHost>   m_remoteHosts;
    UdpConfig           m_config;

    State   m_state = Stopped;
    int     m_maxRetry = 2; // 内部重试次数，不对外暴露
};

#endif // QUDPSOCKETMANAGER_H

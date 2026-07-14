#ifndef QUDPSOCKETMANAGER_H
#define QUDPSOCKETMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QQueue>
#include <QList>

class QUdpSocket;
class QTimer;

// 启动配置：所有可配置参数打包，带默认值
struct UdpConfig
{
    QHostAddress bindAddress = QHostAddress::Any; // 绑定地址，默认所有网卡
    quint16      listenPort = 0;                  // 监听端口，必填
    int          reconnectMs = 3000;              // 重连间隔，默认3秒
    int          maxPacketSize = 1472;            // 最大包长，默认1472(以太网MTU)
    int          maxQueueSize = 128;              // 发送队列最大长度
    int          hostTimeoutSec = 300;            // 主机超时时间，默认5分钟
};
Q_DECLARE_METATYPE(UdpConfig)

// 网络包
struct UdpDatagram
{
    QByteArray    data;
    QHostAddress  host;
    quint16       port = 0;
    qint64        timestamp = 0;
    int           retryCount = 0;
};
Q_DECLARE_METATYPE(UdpDatagram)

// 远程主机信息
struct RemoteHost
{
    QHostAddress address;
    quint16      port = 0;
    qint64       lastSeen = 0;

    bool operator==(const RemoteHost &other) const {
        return address == other.address && port == other.port;
    }
};
Q_DECLARE_METATYPE(RemoteHost)

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

    explicit QUdpSocketManager(QObject *parent = nullptr);
    ~QUdpSocketManager() override;

    // 便捷接口：只传端口启动，其他参数用默认值
    void start(quint16 listenPort);

    // 发送接口
    void sendTo(const QByteArray &data, const QHostAddress &host, quint16 port);
    void sendToAll(const QByteArray &data);
    void broadcast(const QByteArray &data, quint16 targetPort);
    void reply(const QByteArray &data, const UdpDatagram &recvDatagram);

    // 主机管理
    void addRemoteHost(const QHostAddress &host, quint16 port);
    void removeRemoteHost(const QHostAddress &host, quint16 port);
    void clearRemoteHosts();
    QList<RemoteHost> remoteHosts() const;

    // 状态查询
    State state() const;
    bool isRunning() const;
    quint16 listenPort() const;
    UdpConfig currentConfig() const;

public slots:
    // 标准槽函数：可直接连接UI按钮
    void start(const UdpConfig &config); // 传配置启动/重启
    void stop();                         // 停止

signals:
    void datagramReceived(const UdpDatagram &datagram);
    void stateChanged(QUdpSocketManager::State state);
    void errorOccurred(QUdpSocketManager::Error error, const QString &systemErrorString);
    void remoteHostAdded(const RemoteHost &host);
    void remoteHostRemoved(const RemoteHost &host);
    void remoteHostListChanged();

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void processRecv();
    void processSendQueue();
    void doReconnect();
    void cleanupStaleHosts();

private:
    void setState(State newState);
    void updateRemoteHost(const QHostAddress &host, quint16 port);
    Error mapQtSocketError(QAbstractSocket::SocketError err);
    void applyConfig(const UdpConfig &config);

    QUdpSocket *m_socket;
    QTimer     *m_recvTimer;
    QTimer     *m_sendTimer;
    QTimer     *m_reconnectTimer;
    QTimer     *m_hostCleanupTimer;

    QQueue<UdpDatagram> m_sendQueue;
    QList<RemoteHost>   m_remoteHosts;
    UdpConfig           m_config;

    State   m_state = Stopped;
    int     m_maxRetry = 2; // 内部重试次数，不对外暴露
};

#endif // QUDPSOCKETMANAGER_H

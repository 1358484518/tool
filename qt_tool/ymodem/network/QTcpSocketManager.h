#ifndef QTCPSOCKETMANAGER_H
#define QTCPSOCKETMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QQueue>
#include <QAbstractSocket>
#include <QString>

class QTcpSocket;
class QTimer;

/** TCP 客户端参数。bindPort 给客户端时请保持 0，让系统分配源端口。 */
struct TcpConfig
{
    QHostAddress remoteAddress;
    QString      remoteHost;          // 有域名时优先用这个去 connectToHost
    quint16      remotePort = 0;
    QHostAddress bindAddress = QHostAddress::Any; // 指定网卡才 bind，Any 则不 bind
    quint16      bindPort = 0;
    int          connectTimeoutMs = 5000;
    int          reconnectMs = 3000;
    int          maxQueueSize = 128;
    bool         keepAlive = true;
};
Q_DECLARE_METATYPE(TcpConfig)

/** 工作线程里的 TCP 客户端：连接、自动重连、发送队列。重连时换新套接字。 */
class QTcpSocketManager : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Stopped = 0,
        Connecting,
        Connected,
        Reconnecting
    };
    Q_ENUM(State)

    enum Error
    {
        NoError = 0,
        ConnectionRefused,
        HostNotFound,
        ConnectionTimeout,
        RemoteClosed,
        NetworkError,
        PermissionDenied,
        BindError,
        SendError
    };
    Q_ENUM(Error)

    explicit QTcpSocketManager(QObject *parent = nullptr);  // 建发送/重连/连接超时三个定时器
    ~QTcpSocketManager() override;                          // stop 并释放套接字

    void start(const QHostAddress &host, quint16 port);     // 按 IP:端口连，其它参数用默认 TcpConfig
    void send(const QByteArray &data);                      // 已连接则立刻写，否则进发送队列
    void disconnectFromHost();                              // 主动断开，不再自动重连

    State state() const;                                    // Connecting / Connected / Reconnecting
    bool isConnected() const;                               // 是否已经连上
    QHostAddress remoteAddress() const;                     // 当前配置里的对端地址
    quint16 remotePort() const;                             // 当前配置里的对端端口
    TcpConfig currentConfig() const;                        // 拷贝一份当前连接参数

public slots:
    void start(const TcpConfig &config);                    // 按完整配置去连（可指定网卡，不要填 listen 口）
    void stop();                                            // 停定时器、abort 套接字、清空队列

signals:
    void dataReceived(const QByteArray &data);              // 对端来了一段数据
    void stateChanged(QTcpSocketManager::State state);  // Connecting / Connected / Reconnecting
    void connected();                                       // 三次握手完成
    void disconnected();                                    // 套接字已断开
    void errorOccurred(QTcpSocketManager::Error error, const QString &systemErrorString);  // 连接失败、超时、对端关闭等

private slots:
    void onReadyRead();                                     // 有数据立刻读出发出，不攒包
    void onSocketError(QAbstractSocket::SocketError err);   // 映射错误并决定是否重连
    void onConnected();                                     // 连上后刷队列、停连接超时定时器
    void onDisconnected();                                  // 对端关了或本地 abort
    void processRecv();                                     // 兼容旧定时读路径，现在 readyRead 已读完
    void processSendQueue();                                // 把排队的包尽量写出去
    void onConnectTimeout();                                // 连接超时，abort 后走重连
    void doReconnect();                                     // 换新套接字再 connectToHost

private:
    void setState(State newState);                          // 状态变了才发 stateChanged
    Error mapQtSocketError(QAbstractSocket::SocketError err); // Qt 错误码转成本类 Error
    void applyConfig(const TcpConfig &config);              // 保存配置，必要时绑到指定网卡
    void recreateSocket();          // abort 后再 bind 不可靠，直接换新 QTcpSocket
    bool shouldBindLocal() const;   // 只有选了具体网卡才 bind

    QTcpSocket *m_socket = nullptr;
    QTimer     *m_sendTimer;
    QTimer     *m_reconnectTimer;
    QTimer     *m_connectTimer;

    QQueue<QByteArray> m_sendQueue;
    TcpConfig          m_config;

    State   m_state = Stopped;
    int     m_maxRetry = 2;
};

#endif // QTCPSOCKETMANAGER_H

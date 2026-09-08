#ifndef QTCPSERVERMANAGER_H
#define QTCPSERVERMANAGER_H

#include <QAbstractSocket>
#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>

class QTcpServer;
class QTcpSocket;
class QTimer;

typedef quintptr ClientConnId;  // 用套接字指针当连接 id

struct TcpServerConfig
{
    QHostAddress listenAddress = QHostAddress::Any;
    quint16      listenPort = 0;
    int          maxConnections = 32;
    int          reconnectMs = 3000;  // listen 失败后重试间隔
    bool         keepAlive = true;
};
Q_DECLARE_METATYPE(TcpServerConfig)

struct TcpClientInfo
{
    ClientConnId connId = 0;
    QHostAddress peerAddress;
    quint16      peerPort = 0;
    qint64       connectTime = 0;
};
Q_DECLARE_METATYPE(TcpClientInfo)

/** TCP 服务端：listen、多客户端收发、广播。活在网络工作线程。 */
class QTcpServerManager : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Stopped = 0,
        Starting,
        Listening,
        Reconnecting
    };
    Q_ENUM(State)

    enum Error
    {
        NoError = 0,
        ListenFailed,
        AcceptError,
        MaxConnectionsReached,
        PermissionDenied,
        PortInUse,
        ClientError,
        SendError
    };
    Q_ENUM(Error)

    explicit QTcpServerManager(QObject *parent = nullptr);  // 建 QTcpServer 和 listen 失败重试定时器
    ~QTcpServerManager() override;                          // stop 并释放所有客户端套接字

    void start(quint16 listenPort);                         // 只指定端口，地址用 Any
    void sendToClient(ClientConnId connId, const QByteArray &data);  // 发给某一个连接
    void broadcast(const QByteArray &data);                 // 发给当前所有客户端
    void disconnectClient(ClientConnId connId);             // 踢掉指定连接
    QList<TcpClientInfo> clients() const;                   // 当前已连接客户端列表

    State state() const;                                    // Starting / Listening / Reconnecting
    bool isListening() const;                               // 是否已 listen 成功
    quint16 listenPort() const;                             // 当前监听端口
    TcpServerConfig currentConfig() const;                  // 拷贝一份监听参数

public slots:
    void start(const TcpServerConfig &config);              // 按完整配置 listen
    void stop();                                            // 停监听、断开全部客户端

signals:
    void clientConnected(const TcpClientInfo &client);      // 接受了一个新连接
    void clientDisconnected(const TcpClientInfo &client);   // 某个客户端离开
    void clientDataReceived(ClientConnId connId, const QByteArray &data);  // 某个连接来了数据
    void stateChanged(QTcpServerManager::State state);  // Starting / Listening / Reconnecting
    void errorOccurred(QTcpServerManager::Error error, const QString &systemErrorString);  // listen 失败、端口占用等

private slots:
    void onNewConnection();                                 // pending 连接进来，超上限则关掉
    void onClientReadyRead();                               // 某个客户端有数据
    void onClientDisconnected();                            // 某个客户端断开
    void onClientError(QAbstractSocket::SocketError err);    // 某个客户端报错
    void doReconnect();                                     // listen 失败后按间隔再试
    void processClientRecv(QTcpSocket *socket);             // 读该套接字全部 pending 数据并发出

private:
    void setState(State newState);                          // 状态变了才发 stateChanged
    Error mapServerError(QAbstractSocket::SocketError err); // Qt 错误码转成本类 Error
    void applyConfig(const TcpServerConfig &config);        // 保存监听地址、端口、连接上限
    TcpClientInfo getClientInfo(QTcpSocket *socket) const;  // 从套接字填出对端 IP/端口
    void removeClient(QTcpSocket *socket);                  // 从表里拿掉并 deleteLater

    QTcpServer *m_server;
    QTimer     *m_reconnectTimer;
    QMap<ClientConnId, QTcpSocket*> m_clients;
    TcpServerConfig m_config;

    State   m_state = Stopped;
};

#endif // QTCPSERVERMANAGER_H

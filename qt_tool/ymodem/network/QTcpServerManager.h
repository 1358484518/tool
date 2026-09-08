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

typedef quintptr ClientConnId;

// 服务器配置
struct TcpServerConfig
{
    QHostAddress listenAddress = QHostAddress::Any; // 监听地址，默认所有网卡
    quint16      listenPort = 0;                    // 监听端口，必填
    int          maxConnections = 32;               // 最大客户端连接数
    int          reconnectMs = 3000;                // 监听失败重连间隔
    bool         keepAlive = true;                  // 客户端连接开启TCP KeepAlive
};
Q_DECLARE_METATYPE(TcpServerConfig)

// 客户端信息
struct TcpClientInfo
{
    ClientConnId connId = 0;
    QHostAddress peerAddress;
    quint16      peerPort = 0;
    qint64       connectTime = 0;
};
Q_DECLARE_METATYPE(TcpClientInfo)

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

    explicit QTcpServerManager(QObject *parent = nullptr);
    ~QTcpServerManager() override;

    // 便捷接口
    void start(quint16 listenPort);

    // 操作接口
    void sendToClient(ClientConnId connId, const QByteArray &data);
    void broadcast(const QByteArray &data);
    void disconnectClient(ClientConnId connId);
    QList<TcpClientInfo> clients() const;

    // 状态查询
    State state() const;
    bool isListening() const;
    quint16 listenPort() const;
    TcpServerConfig currentConfig() const;

public slots:
    void start(const TcpServerConfig &config);
    void stop();

signals:
    void clientConnected(const TcpClientInfo &client);
    void clientDisconnected(const TcpClientInfo &client);
    void clientDataReceived(ClientConnId connId, const QByteArray &data);
    void stateChanged(QTcpServerManager::State state);
    void errorOccurred(QTcpServerManager::Error error, const QString &systemErrorString);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onClientError(QAbstractSocket::SocketError err);
    void doReconnect();
    void processClientRecv(QTcpSocket *socket);

private:
    void setState(State newState);
    Error mapServerError(QAbstractSocket::SocketError err);
    void applyConfig(const TcpServerConfig &config);
    TcpClientInfo getClientInfo(QTcpSocket *socket) const;
    void removeClient(QTcpSocket *socket);

    QTcpServer *m_server;
    QTimer     *m_reconnectTimer;
    QMap<ClientConnId, QTcpSocket*> m_clients;
    TcpServerConfig m_config;

    State   m_state = Stopped;
};

#endif // QTCPSERVERMANAGER_H

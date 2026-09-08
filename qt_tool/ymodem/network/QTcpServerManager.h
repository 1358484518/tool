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

    explicit QTcpServerManager(QObject *parent = nullptr);
    ~QTcpServerManager() override;

    void start(quint16 listenPort);
    void sendToClient(ClientConnId connId, const QByteArray &data);
    void broadcast(const QByteArray &data);
    void disconnectClient(ClientConnId connId);
    QList<TcpClientInfo> clients() const;

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

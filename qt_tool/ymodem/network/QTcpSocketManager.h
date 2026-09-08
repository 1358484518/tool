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

    explicit QTcpSocketManager(QObject *parent = nullptr);
    ~QTcpSocketManager() override;

    void start(const QHostAddress &host, quint16 port);
    void send(const QByteArray &data);
    void disconnectFromHost();

    State state() const;
    bool isConnected() const;
    QHostAddress remoteAddress() const;
    quint16 remotePort() const;
    TcpConfig currentConfig() const;

public slots:
    void start(const TcpConfig &config);
    void stop();

signals:
    void dataReceived(const QByteArray &data);
    void stateChanged(QTcpSocketManager::State state);
    void connected();
    void disconnected();
    void errorOccurred(QTcpSocketManager::Error error, const QString &systemErrorString);

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onConnected();
    void onDisconnected();
    void processRecv();
    void processSendQueue();
    void onConnectTimeout();
    void doReconnect();

private:
    void setState(State newState);
    Error mapQtSocketError(QAbstractSocket::SocketError err);
    void applyConfig(const TcpConfig &config);
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

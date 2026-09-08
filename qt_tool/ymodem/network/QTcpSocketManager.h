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

// TCP连接配置
struct TcpConfig
{
    QHostAddress remoteAddress;       // 远程 IP，可为空（走域名）
    QString      remoteHost;          // 域名或 IP 文本，优先用于 connectToHost
    quint16      remotePort = 0;      // 远程端口
    QHostAddress bindAddress = QHostAddress::Any; // 本地绑定地址
    quint16      bindPort = 0;        // 本地绑定端口，0自动分配
    int          connectTimeoutMs = 5000; // 连接超时，默认5秒
    int          reconnectMs = 3000;  // 重连间隔，默认3秒
    int          maxQueueSize = 128;  // 发送队列最大长度
    bool         keepAlive = true;    // 开启TCP KeepAlive
};
Q_DECLARE_METATYPE(TcpConfig)

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

    // 便捷接口
    void start(const QHostAddress &host, quint16 port);

    // 操作接口
    void send(const QByteArray &data);
    void disconnectFromHost();

    // 状态查询
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
    void recreateSocket();
    bool shouldBindLocal() const;

    QTcpSocket *m_socket = nullptr;
    QTimer     *m_recvTimer;
    QTimer     *m_sendTimer;
    QTimer     *m_reconnectTimer;
    QTimer     *m_connectTimer;

    QQueue<QByteArray> m_sendQueue;
    TcpConfig          m_config;

    State   m_state = Stopped;
    int     m_maxRetry = 2;
};

#endif // QTCPSOCKETMANAGER_H

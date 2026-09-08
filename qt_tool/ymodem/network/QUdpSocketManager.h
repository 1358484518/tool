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

    explicit QUdpSocketManager(QObject *parent = nullptr);
    ~QUdpSocketManager() override;

    void start(quint16 listenPort);

    void sendTo(const QByteArray &data, const QHostAddress &host, quint16 port);
    void sendToAll(const QByteArray &data);
    void broadcast(const QByteArray &data, quint16 targetPort);
    void reply(const QByteArray &data, const UdpDatagram &recvDatagram);

    void addRemoteHost(const QHostAddress &host, quint16 port);
    void removeRemoteHost(const QHostAddress &host, quint16 port);
    void clearRemoteHosts();
    QList<RemoteHost> remoteHosts() const;

    State state() const;
    bool isRunning() const;
    quint16 listenPort() const;
    UdpConfig currentConfig() const;

public slots:
    void start(const UdpConfig &config);
    void stop();

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

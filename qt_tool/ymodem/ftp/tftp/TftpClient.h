#pragma once

#include "TftpProtocol.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

namespace TftpQt {

class TftpClient : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString host;
        quint16 port = 69;
        QString remoteFile;
        QString localFile;
        bool download = true;
        int timeoutSec = 5;
        int maxRetries = 5;
        int blockSize = 0;
        bool timeoutOption = false;
        bool transferSizeOption = false;
    };

    explicit TftpClient(QObject *parent = nullptr);

    bool isActive() const;
    void start(const Request &request);
    void cancel();

    int negotiatedBlockSize() const;
    int negotiatedTimeout() const;
    qint64 announcedSize() const;

signals:
    void logMessage(const QString &message);
    void progress(qint64 transferred, qint64 total);
    void finished(bool success, const QString &message);

private slots:
    void onLookup(const QHostInfo &info);

private:
    enum class Phase {
        Idle,
        Resolving,
        WaitFirst,
        Recv,
        Send,
        Dally
    };

    void beginTransfer();
    void onReadyRead();
    void onTimeout();
    void handlePacket(const QByteArray &datagram, const QHostAddress &sender, quint16 senderPort);
    void handleFirst(const struct Packet &packet, quint16 senderPort);
    void handleReceive(const struct Packet &packet);
    void handleSend(const struct Packet &packet);
    bool applyOack(const struct Packet &packet, QString *errorMessage);
    bool transmit(const QByteArray &datagram);
    void sendNextData();
    void enterDally();
    void succeed(const QString &message);
    void fail(const QString &message);
    void reportProgress();
    QString validate(const Request &request) const;

    Request request;
    Phase phase = Phase::Idle;
    bool active = false;
    bool discardLocalOnFail = false;
    bool waitingAck0 = false;
    bool sentData = false;
    bool shortSent = false;
    bool peerPortKnown = false;
    int lookupId = -1;
    int timeoutSec = 5;
    int maxRetries = 5;
    int attempt = 0;
    int blockSize = DefaultBlockSize;
    int txCount = 0;
    int rxCount = 0;
    quint16 peerPort = 0;
    quint16 lastBlock = 0;
    quint16 expectBlock = 1;
    qint64 bytesTransferred = 0;
    qint64 totalSize = -1;
    QHostAddress peer;
    QByteArray lastPacket;
    QFile file;
    QUdpSocket *socket = nullptr;
    QTimer timer;
    QElapsedTimer elapsed;
};

} // namespace TftpQt

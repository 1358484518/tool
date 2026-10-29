#pragma once

#include "TftpProtocol.h"

#include <QHash>
#include <QObject>

class QUdpSocket;

namespace TftpQt {

class TftpServer : public QObject {
    Q_OBJECT

public:
    struct Settings {
        quint16 port = 69;
        QString root;
        int timeoutSec = 5;
        int maxRetries = 5;
        bool allowBlockSize = true;
        int maxBlockSize = EthernetBlockSize;
        bool allowTimeout = true;
        bool allowTransferSize = true;
    };

    explicit TftpServer(QObject *parent = nullptr);
    ~TftpServer() override;

    bool isRunning() const;
    quint16 port() const;
    int sessionCount() const;

    bool start(const Settings &settings, QString *errorMessage);
    void stop();

signals:
    void logMessage(const QString &message);
    void sessionCountChanged(int count);

private slots:
    void onReadyRead();

private:
    class Session;
    friend class Session;

    void takeRequest(const QByteArray &datagram, const QHostAddress &sender, quint16 senderPort);
    void sendError(const QHostAddress &sender, quint16 senderPort, quint16 code, const QByteArray &message);
    void forgetSession(Session *session);

    Settings settings;
    QUdpSocket *listenSocket = nullptr;
    quint16 boundPort = 0;
    bool running = false;
    QHash<QString, Session *> sessions;
};

} // namespace TftpQt

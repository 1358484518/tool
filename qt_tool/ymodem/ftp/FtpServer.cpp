#include "FtpServer.h"
#include "FtpControlConnection.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

FtpServer::FtpServer(QObject *parent, const QString &rootPath, quint16 port,
                     const QString &userName, const QString &password, bool readOnly, bool onlyOneIp)
    : QObject(parent)
    , m_userName(userName)
    , m_password(password)
    , m_rootPath(rootPath)
    , m_readOnly(readOnly)
    , m_onlyOneIp(onlyOneIp)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &FtpServer::startNewControlConnection);
    // 只听 IPv4。21 端口在 Windows 上经常要管理员权限，调用方默认用 2121。
    if (!m_server->listen(QHostAddress::AnyIPv4, port))
        m_error = m_server->errorString();
}

bool FtpServer::isListening() const
{
    return m_server && m_server->isListening();
}

QString FtpServer::errorString() const
{
    return m_error;
}

QString FtpServer::lanIp()
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address != QHostAddress(QHostAddress::LocalHost))
            return address.toString();
    }
    return QString();
}

void FtpServer::startNewControlConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket)
        return;
    const QString peerIp = socket->peerAddress().toString();
    // 只记第一个地址。重启服务会换一个 FtpServer，这个集合跟着清掉。
    if (m_onlyOneIp && !m_seenIps.isEmpty() && !m_seenIps.contains(peerIp)) {
        emit logLine(tr("Rejected %1").arg(peerIp));
        delete socket;
        return;
    }
    const bool firstFromThisIp = !m_seenIps.contains(peerIp);
    m_seenIps.insert(peerIp);
    if (firstFromThisIp)
        emit peerConnected(peerIp);
    auto *connection = new FtpControlConnection(this, socket, m_rootPath, m_userName, m_password, m_readOnly);
    connect(connection, &FtpControlConnection::activity, this, &FtpServer::logLine);
    // 欢迎语放在信号接上之后，否则界面收不到第一行。
    connection->opened();
}

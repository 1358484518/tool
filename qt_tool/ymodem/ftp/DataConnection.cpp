#include "DataConnection.h"
#include "FtpCommand.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

DataConnection::DataConnection(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &DataConnection::onNewConnection);
}

void DataConnection::scheduleConnectToHost(const QString &hostName, int port)
{
    delete m_socket;
    m_socket = nullptr;
    m_hostName = hostName;
    m_port = port;
    m_socketReady = false;
    m_waitingForCommand = true;
    m_active = true;
}

int DataConnection::listen()
{
    delete m_socket;
    m_socket = nullptr;
    delete m_command;
    m_command = nullptr;
    m_socketReady = false;
    m_waitingForCommand = true;
    m_active = false;
    m_server->close();
    // 被动端口只绑 IPv4，PASV 应答里才能写成 h1,h2,h3,h4,p1,p2。
    if (!m_server->listen(QHostAddress::AnyIPv4, 0))
        return 0;
    return m_server->serverPort();
}

bool DataConnection::setFtpCommand(FtpCommand *command)
{
    if (!m_waitingForCommand) {
        return false;
    }
    m_waitingForCommand = false;
    m_command = command;
    command->setParent(this);

    if (m_active) {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &DataConnection::onConnected);
        m_socket->connectToHost(m_hostName, static_cast<quint16>(m_port));
    } else {
        startFtpCommand();
    }
    return true;
}

FtpCommand *DataConnection::ftpCommand()
{
    if (m_socketReady)
        return m_command;
    return nullptr;
}

void DataConnection::onNewConnection()
{
    m_socket = m_server->nextPendingConnection();
    m_server->close();
    m_socketReady = true;
    startFtpCommand();
}

void DataConnection::onConnected()
{
    m_socketReady = true;
    startFtpCommand();
}

void DataConnection::startFtpCommand()
{
    if (m_command && m_socketReady && m_socket) {
        QTcpSocket *socket = m_socket;
        m_socket = nullptr;
        m_command->start(socket);
    }
}

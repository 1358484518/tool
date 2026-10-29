#include "FtpCommand.h"

#include <QTcpSocket>

FtpCommand::FtpCommand(QObject *parent)
    : QObject(parent)
{
}

void FtpCommand::start(QTcpSocket *socket)
{
    m_started = true;
    m_socket = socket;
    socket->setParent(this);
    connect(socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
    startImplementation();
}

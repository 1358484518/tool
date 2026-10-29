#include "FtpRetrCommand.h"

#include <QFile>
#include <QTcpSocket>

FtpRetrCommand::FtpRetrCommand(QObject *parent, const QString &fileName, qint64 seekTo)
    : FtpCommand(parent)
    , m_fileName(fileName)
    , m_seekTo(seekTo)
{
}

FtpRetrCommand::~FtpRetrCommand()
{
    if (!m_started || m_failed)
        return;
    if (m_file && m_file->isOpen() && m_file->atEnd())
        emit reply(QStringLiteral("226 Closing data connection."));
    else
        emit reply(QStringLiteral("550 Requested action not taken; file unavailable."));
}

void FtpRetrCommand::startImplementation()
{
    m_file = new QFile(m_fileName, this);
    if (!m_file->open(QIODevice::ReadOnly)) {
        m_failed = true;
        deleteLater();
        return;
    }
    emit reply(QStringLiteral("150 File status okay; about to open data connection."));
    if (m_seekTo)
        m_file->seek(m_seekTo);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &FtpRetrCommand::refillSocketBuffer);
    refillSocketBuffer(128 * 1024);
}

void FtpRetrCommand::refillSocketBuffer(qint64 bytes)
{
    if (!m_file->atEnd())
        m_socket->write(m_file->read(bytes));
    else
        m_socket->disconnectFromHost();
}

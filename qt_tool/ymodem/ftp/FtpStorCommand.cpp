#include "FtpStorCommand.h"

#include <QFile>
#include <QTcpSocket>

FtpStorCommand::FtpStorCommand(QObject *parent, const QString &fileName, bool appendMode, qint64 seekTo)
    : FtpCommand(parent)
    , m_fileName(fileName)
    , m_appendMode(appendMode)
    , m_seekTo(seekTo)
{
}

FtpStorCommand::~FtpStorCommand()
{
    if (!m_started)
        return;
    if (m_success)
        emit reply(QStringLiteral("226 Closing data connection."));
    else
        emit reply(QStringLiteral("451 Requested action aborted: local error in processing."));
}

void FtpStorCommand::startImplementation()
{
    m_file = new QFile(m_fileName, this);
    const QIODevice::OpenMode mode = m_appendMode ? QIODevice::Append : QIODevice::WriteOnly;
    if (!m_file->open(mode)) {
        deleteLater();
        return;
    }
    m_success = true;
    emit reply(QStringLiteral("150 File status okay; about to open data connection."));
    if (m_seekTo)
        m_file->seek(m_seekTo);
    connect(m_socket, &QTcpSocket::readyRead, this, &FtpStorCommand::acceptNextBlock);
    if (m_socket->bytesAvailable() > 0)
        acceptNextBlock();
}

void FtpStorCommand::acceptNextBlock()
{
    const QByteArray bytes = m_socket->readAll();
    const qint64 written = m_file->write(bytes);
    if (written == bytes.size())
        return;
    // 失败标记留给析构回 451，这里再回一次会和析构重复。
    m_success = false;
    deleteLater();
}

#include "FtpListCommand.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTcpSocket>
#include <QTimer>

namespace {

QString padded(const QString &text, int width)  // 左侧补空格，让列表列对齐
{
    QString out = text;
    while (out.size() < width)
        out.prepend(QLatin1Char(' '));
    return out;
}

} // namespace

FtpListCommand::FtpListCommand(QObject *parent, const QString &listDirectory, bool nameListOnly)
    : FtpCommand(parent)
    , m_listDirectory(listDirectory)
    , m_nameListOnly(nameListOnly)
{
}

FtpListCommand::~FtpListCommand()
{
    if (m_started && !m_failed)
        emit reply(QStringLiteral("226 Closing data connection."));
}

void FtpListCommand::startImplementation()
{
    const QFileInfo info(m_listDirectory);
    if (!info.isReadable()) {
        m_failed = true;
        emit reply(QStringLiteral("425 File or directory is not readable or doesn't exist."));
        m_socket->disconnectFromHost();
        return;
    }

    emit reply(QStringLiteral("150 File status okay; about to open data connection."));
    if (!info.isDir())
        m_list = QFileInfoList() << info;
    else
        m_list = QDir(m_listDirectory).entryInfoList();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &FtpListCommand::listNextBatch);
    m_timer->start(0);
}

QString FtpListCommand::fileListingString(const QFileInfo &fi)
{
    QString line;
    if (!m_nameListOnly) {
        if (fi.isSymLink())
            line += QLatin1Char('l');
        else if (fi.isDir())
            line += QLatin1Char('d');
        else
            line += QLatin1Char('-');

        const QFile::Permissions p = fi.permissions();
        line += (p & QFile::ReadOwner) ? QLatin1Char('r') : QLatin1Char('-');
        line += (p & QFile::WriteOwner) ? QLatin1Char('w') : QLatin1Char('-');
        line += (p & QFile::ExeOwner) ? QLatin1Char('x') : QLatin1Char('-');
        line += (p & QFile::ReadGroup) ? QLatin1Char('r') : QLatin1Char('-');
        line += (p & QFile::WriteGroup) ? QLatin1Char('w') : QLatin1Char('-');
        line += (p & QFile::ExeGroup) ? QLatin1Char('x') : QLatin1Char('-');
        line += (p & QFile::ReadOther) ? QLatin1Char('r') : QLatin1Char('-');
        line += (p & QFile::WriteOther) ? QLatin1Char('w') : QLatin1Char('-');
        line += (p & QFile::ExeOther) ? QLatin1Char('x') : QLatin1Char('-');

        QString owner = fi.owner();
        if (owner.isEmpty())
            owner = QStringLiteral("unknown");
        QString group = fi.group();
        if (group.isEmpty())
            group = QStringLiteral("unknown");
        line += QLatin1Char(' ') + padded(owner, 10) + QLatin1Char(' ') + padded(group, 10);
        line += QLatin1Char(' ') + padded(QString::number(fi.size()), 14);

        // 月份必须是英文，否则部分客户端解析不了 LIST。
        const QLocale locale(QLocale::English);
        const QDateTime lm = fi.lastModified();
        if (lm.date().year() != QDate::currentDate().year())
            line += QLatin1Char(' ') + locale.toString(lm.date(), QStringLiteral("MMM dd  yyyy"));
        else
            line += QLatin1Char(' ') + locale.toString(lm.date(), QStringLiteral("MMM dd"))
                    + QLatin1Char(' ') + locale.toString(lm.time(), QStringLiteral("hh:mm"));
    }
    line += QLatin1Char(' ') + fi.fileName();
    line += QStringLiteral("\r\n");
    return line;
}

void FtpListCommand::listNextBatch()
{
    const int stop = qMin(m_index + 10, m_list.size());
    while (m_index < stop) {
        m_socket->write(fileListingString(m_list.at(m_index)).toUtf8());
        ++m_index;
    }
    if (m_list.size() != stop)
        return;
    m_timer->stop();
    m_socket->disconnectFromHost();
}

#include "FtpControlConnection.h"
#include "DataConnection.h"
#include "FtpListCommand.h"
#include "FtpRetrCommand.h"
#include "FtpStorCommand.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

FtpControlConnection::FtpControlConnection(QObject *parent, QTcpSocket *socket, const QString &rootPath,
                                           const QString &userName, const QString &password, bool readOnly)
    : QObject(parent)
    , m_socket(socket)
    , m_userName(userName)
    , m_password(password)
    , m_rootPath(rootPath)
    , m_readOnly(readOnly)
{
    m_socket->setParent(this);
    m_peerIp = m_socket->peerAddress().toString();
    m_currentDirectory = QStringLiteral("/");
    m_dataConnection = new DataConnection(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &FtpControlConnection::acceptNewData);
    connect(m_socket, &QTcpSocket::disconnected, this, &FtpControlConnection::onDisconnected);
}

void FtpControlConnection::opened()
{
    emit activity(tr("Client %1 connected").arg(m_peerIp));
    reply(QStringLiteral("220 Welcome."));
}

void FtpControlConnection::onDisconnected()
{
    emit activity(tr("Client %1 disconnected").arg(m_peerIp));
    deleteLater();
}

FtpControlConnection::~FtpControlConnection()
{
    // 父对象销毁时套接字还会发 disconnected。先拆掉，避免析构过程里再 deleteLater。
    if (m_socket)
        m_socket->disconnect(this);
}

void FtpControlConnection::acceptNewData()
{
    if (!m_socket || !m_socket->canReadLine())
        return;
    // 一次只执行一行，剩下的交给下一轮事件，避免一个客户端堵住别的连接。
    processCommand(QString::fromUtf8(m_socket->readLine()).trimmed());
    QTimer::singleShot(0, this, &FtpControlConnection::acceptNewData);
}

void FtpControlConnection::disconnectFromHost()
{
    if (m_socket)
        m_socket->disconnectFromHost();
}

bool FtpControlConnection::verifyAuthentication(const QString &command)
{
    if (m_loggedIn)
        return true;

    const char *commandsRequiringAuth[] = {
        "PWD", "CWD", "TYPE", "PORT", "PASV", "LIST", "RETR", "REST",
        "NLST", "SIZE", "SYST", "CDUP", "OPTS", "NOOP",
        "STOR", "MKD", "RMD", "DELE", "RNFR", "RNTO", "APPE"
    };
    for (const char *name : commandsRequiringAuth) {
        if (command == QLatin1String(name)) {
            reply(QStringLiteral("530 You must log in first."));
            return false;
        }
    }
    return true;
}

bool FtpControlConnection::verifyWritePermission(const QString &command)
{
    if (!m_readOnly)
        return true;

    const char *commandsRequiringWrite[] = {
        "STOR", "MKD", "RMD", "DELE", "RNFR", "RNTO", "APPE"
    };
    for (const char *name : commandsRequiringWrite) {
        if (command == QLatin1String(name)) {
            reply(QStringLiteral("550 Can't do that in read-only mode."));
            return false;
        }
    }
    return true;
}

QString FtpControlConnection::stripFlagL(const QString &fileName)
{
    const QString upper = fileName.toUpper();
    if (upper == QLatin1String("-L"))
        return QString();
    if (upper.startsWith(QLatin1String("-L ")))
        return fileName.mid(3);
    return fileName;
}

void FtpControlConnection::parseCommand(const QString &entireCommand, QString *command, QString *commandParameters)
{
    const int pos = entireCommand.indexOf(QLatin1Char(' '));
    if (pos >= 0) {
        *command = entireCommand.left(pos).trimmed().toUpper();
        *commandParameters = entireCommand.mid(pos + 1).trimmed();
    } else {
        *command = entireCommand.trimmed().toUpper();
        commandParameters->clear();
    }
}

QString FtpControlConnection::toLocalPath(const QString &fileName) const
{
    QString localPath = fileName;
    localPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!localPath.startsWith(QLatin1Char('/')))
        localPath = m_currentDirectory + QLatin1Char('/') + localPath;

    // 先消化 . 和 ..，再拼根目录，避免客户端用 .. 跑出根目录。
    QStringList components;
    const QStringList raw = localPath.split(QLatin1Char('/'));
    for (const QString &component : raw) {
        if (component.isEmpty() || component == QLatin1String("."))
            continue;
        if (component == QLatin1String("..")) {
            if (!components.isEmpty())
                components.removeLast();
            continue;
        }
        components += component;
    }
    return QDir::cleanPath(m_rootPath + QLatin1Char('/') + components.join(QLatin1Char('/')));
}

void FtpControlConnection::reply(const QString &replyCode)
{
    emit activity(QStringLiteral("< ") + replyCode);
    if (m_socket)
        m_socket->write((replyCode + QStringLiteral("\r\n")).toUtf8());
}

void FtpControlConnection::processCommand(const QString &entireCommand)
{
    QString command;
    QString commandParameters;
    parseCommand(entireCommand, &command, &commandParameters);

    // PASS 参数是密码，日志里不能留明文。
    if (command == QLatin1String("PASS"))
        emit activity(QStringLiteral("> PASS ****"));
    else
        emit activity(QStringLiteral("> ") + entireCommand);

    if (!verifyAuthentication(command))
        return;
    if (!verifyWritePermission(command))
        return;

    if (command == QLatin1String("USER"))
        reply(QStringLiteral("331 User name OK, need password."));
    else if (command == QLatin1String("PASS"))
        pass(commandParameters);
    else if (command == QLatin1String("QUIT"))
        quit();
    else if (command == QLatin1String("FEAT"))
        feat();
    else if (command == QLatin1String("PWD"))
        reply(QStringLiteral("257 \"%1\"").arg(m_currentDirectory));
    else if (command == QLatin1String("CWD"))
        cwd(commandParameters);
    else if (command == QLatin1String("TYPE"))
        reply(QStringLiteral("200 Command okay."));
    else if (command == QLatin1String("PORT"))
        port(commandParameters);
    else if (command == QLatin1String("PASV"))
        pasv();
    else if (command == QLatin1String("LIST"))
        list(toLocalPath(stripFlagL(commandParameters)), false);
    else if (command == QLatin1String("RETR"))
        retr(toLocalPath(commandParameters));
    else if (command == QLatin1String("REST"))
        reply(QStringLiteral("350 Requested file action pending further information."));
    else if (command == QLatin1String("NLST"))
        list(toLocalPath(stripFlagL(commandParameters)), true);
    else if (command == QLatin1String("SIZE"))
        size(toLocalPath(commandParameters));
    else if (command == QLatin1String("SYST"))
        reply(QStringLiteral("215 UNIX"));
    else if (command == QLatin1String("CDUP"))
        cdup();
    else if (command == QLatin1String("OPTS") && commandParameters.toUpper() == QLatin1String("UTF8 ON"))
        reply(QStringLiteral("200 Command okay."));
    else if (command == QLatin1String("NOOP"))
        reply(QStringLiteral("200 Command okay."));
    else if (command == QLatin1String("STOR"))
        stor(toLocalPath(commandParameters));
    else if (command == QLatin1String("MKD"))
        mkd(toLocalPath(commandParameters));
    else if (command == QLatin1String("RMD"))
        rmd(toLocalPath(commandParameters));
    else if (command == QLatin1String("DELE"))
        dele(toLocalPath(commandParameters));
    else if (command == QLatin1String("RNFR"))
        reply(QStringLiteral("350 Requested file action pending further information."));
    else if (command == QLatin1String("RNTO"))
        rnto(toLocalPath(commandParameters));
    else if (command == QLatin1String("APPE"))
        stor(toLocalPath(commandParameters), true);
    else
        reply(QStringLiteral("502 Command not implemented."));

    m_lastProcessedCommand = entireCommand;
}

void FtpControlConnection::startOrScheduleCommand(FtpCommand *ftpCommand)
{
    connect(ftpCommand, &FtpCommand::reply, this, &FtpControlConnection::reply);
    if (!m_dataConnection->setFtpCommand(ftpCommand)) {
        delete ftpCommand;
        reply(QStringLiteral("425 Can't open data connection."));
    }
}

void FtpControlConnection::port(const QString &addressAndPort)
{
    const QRegularExpression exp(QStringLiteral("\\s*(\\d+,\\d+,\\d+,\\d+),(\\d+),(\\d+)"));
    const QRegularExpressionMatch match = exp.match(addressAndPort);
    if (!match.hasMatch()) {
        reply(QStringLiteral("501 Syntax error in parameters or arguments."));
        return;
    }
    const QString hostName = QString(match.captured(1)).replace(QLatin1Char(','), QLatin1Char('.'));
    const int port = match.captured(2).toInt() * 256 + match.captured(3).toInt();
    m_dataConnection->scheduleConnectToHost(hostName, port);
    reply(QStringLiteral("200 Command okay."));
}

void FtpControlConnection::pasv()
{
    const int port = m_dataConnection->listen();
    if (port <= 0) {
        reply(QStringLiteral("425 Can't open data connection."));
        return;
    }
    const QString host = m_socket->localAddress().toString().replace(QLatin1Char('.'), QLatin1Char(','));
    reply(QStringLiteral("227 Entering Passive Mode (%1,%2,%3).").arg(host).arg(port / 256).arg(port % 256));
}

void FtpControlConnection::list(const QString &dir, bool nameListOnly)
{
    startOrScheduleCommand(new FtpListCommand(this, dir, nameListOnly));
}

void FtpControlConnection::retr(const QString &fileName)
{
    startOrScheduleCommand(new FtpRetrCommand(this, fileName, seekTo()));
}

void FtpControlConnection::stor(const QString &fileName, bool appendMode)
{
    startOrScheduleCommand(new FtpStorCommand(this, fileName, appendMode, seekTo()));
}

void FtpControlConnection::cwd(const QString &dir)
{
    const QFileInfo local(toLocalPath(dir));
    if (!local.exists() || !local.isDir()) {
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
        return;
    }
    if (dir.startsWith(QLatin1Char('/')))
        m_currentDirectory = QDir::cleanPath(dir);
    else
        m_currentDirectory = QDir::cleanPath(m_currentDirectory + QLatin1Char('/') + dir);
    reply(QStringLiteral("250 Requested file action okay, completed."));
}

void FtpControlConnection::mkd(const QString &dir)
{
    if (QDir().mkdir(dir))
        reply(QStringLiteral("257 \"%1\" created.").arg(dir));
    else
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
}

void FtpControlConnection::rmd(const QString &dir)
{
    if (QDir().rmdir(dir))
        reply(QStringLiteral("250 Requested file action okay, completed."));
    else
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
}

void FtpControlConnection::dele(const QString &fileName)
{
    if (QFile::remove(fileName))
        reply(QStringLiteral("250 Requested file action okay, completed."));
    else
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
}

void FtpControlConnection::rnto(const QString &fileName)
{
    QString command;
    QString commandParameters;
    parseCommand(m_lastProcessedCommand, &command, &commandParameters);
    if (command == QLatin1String("RNFR") && QFile::rename(toLocalPath(commandParameters), fileName))
        reply(QStringLiteral("250 Requested file action okay, completed."));
    else
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
}

void FtpControlConnection::quit()
{
    reply(QStringLiteral("221 Quitting..."));
    if (m_dataConnection->ftpCommand())
        connect(m_dataConnection->ftpCommand(), &QObject::destroyed, this, &FtpControlConnection::disconnectFromHost);
    else
        disconnectFromHost();
}

void FtpControlConnection::size(const QString &fileName)
{
    const QFileInfo fi(fileName);
    if (!fi.exists() || fi.isDir())
        reply(QStringLiteral("550 Requested action not taken; file unavailable."));
    else
        reply(QStringLiteral("213 %1").arg(fi.size()));
}

void FtpControlConnection::pass(const QString &password)
{
    QString command;
    QString commandParameters;
    parseCommand(m_lastProcessedCommand, &command, &commandParameters);
    // 用户名和密码都空才是匿名。只填了用户名时，密码也必须是空字符串才算对。
    const bool anonymous = m_userName.isEmpty() && m_password.isEmpty();
    const bool matched = command == QLatin1String("USER")
                         && m_userName == commandParameters
                         && m_password == password;
    if (anonymous || matched) {
        reply(QStringLiteral("230 You are logged in."));
        m_loggedIn = true;
    } else {
        reply(QStringLiteral("530 User name or password was incorrect."));
    }
}

void FtpControlConnection::cdup()
{
    if (m_currentDirectory == QLatin1String("/"))
        reply(QStringLiteral("250 Requested file action okay, completed."));
    else
        cwd(QStringLiteral(".."));
}

void FtpControlConnection::feat()
{
    if (!m_socket)
        return;
    m_socket->write("211-Features:\r\n UTF8\r\n211 End\r\n");
    emit activity(QStringLiteral("< 211 End"));
}

qint64 FtpControlConnection::seekTo()
{
    QString command;
    QString commandParameters;
    parseCommand(m_lastProcessedCommand, &command, &commandParameters);
    if (command != QLatin1String("REST"))
        return 0;
    return commandParameters.toLongLong();
}

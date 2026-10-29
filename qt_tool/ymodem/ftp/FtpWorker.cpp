#include "FtpWorker.h"
#include "FtpServer.h"

#include <QDir>

FtpWorker::FtpWorker(QObject *parent)
    : QObject(parent)
{
}

FtpWorker::~FtpWorker()
{
    stopServer();
}

void FtpWorker::startServer(quint16 port, QString root, QString user, QString password, bool readOnly, bool onlyOneIp)
{
    // 重新监听时不要先刷一条“已停止”，界面会看成闪一下。
    closeServer(false);
    root = QDir::cleanPath(root);
    if (root.isEmpty() || !QDir(root).exists()) {
        emit serverState(false, tr("Root directory does not exist"));
        return;
    }

    m_server = new FtpServer(this, root, port, user, password, readOnly, onlyOneIp);
    connect(m_server, &FtpServer::logLine, this, &FtpWorker::logLine);
    connect(m_server, &FtpServer::peerConnected, this, &FtpWorker::peerConnected);
    if (!m_server->isListening()) {
        const QString err = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        emit logLine(tr("Failed to listen: %1").arg(err));
        emit serverState(false, tr("Not listening"));
        return;
    }

    QString ip = FtpServer::lanIp();
    if (ip.isEmpty())
        ip = QStringLiteral("127.0.0.1");
    const QString message = tr("Listening at %1").arg(ip);
    emit serverState(true, message);
    emit logLine(message);
}

void FtpWorker::stopServer()
{
    closeServer(true);
}

void FtpWorker::closeServer(bool notify)
{
    if (!m_server)
        return;
    delete m_server;
    m_server = nullptr;
    if (!notify)
        return;
    const QString message = tr("Server stopped");
    emit serverState(false, message);
    emit logLine(message);
}

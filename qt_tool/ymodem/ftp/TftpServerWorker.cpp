#include "TftpServerWorker.h"

#include "TftpServer.h"

#include <QDir>

TftpServerWorker::TftpServerWorker(QObject *parent)
    : QObject(parent)
{
}

TftpServerWorker::~TftpServerWorker()
{
    stopServer();
}

void TftpServerWorker::ensureServer()
{
    if (m_server)
        return;
    // 这个槽跑在工作线程上，UDP 套接字跟着 TftpServer 生在这里。
    m_server = new TftpQt::TftpServer(this);
    connect(m_server, &TftpQt::TftpServer::logMessage, this, &TftpServerWorker::logLine);
    connect(m_server, &TftpQt::TftpServer::sessionCountChanged, this, [this](int count) {
        if (!m_server->isRunning())
            return;
        emit runningChanged(true, tr("Listening at %1, %2 session(s)").arg(m_server->port()).arg(count));
    });
}

void TftpServerWorker::startServer(quint16 port, QString root)
{
    ensureServer();
    if (m_server->isRunning())
        m_server->stop();

    root = QDir::cleanPath(root);
    if (root.isEmpty() || !QDir(root).exists()) {
        emit runningChanged(false, tr("Root directory does not exist"));
        return;
    }

    TftpQt::TftpServer::Settings settings;
    settings.port = port;
    settings.root = root;
    QString error;
    if (!m_server->start(settings, &error)) {
        emit logLine(error);
        emit runningChanged(false, tr("Not listening"));
        return;
    }
    emit runningChanged(true, tr("Listening at %1").arg(m_server->port()));
}

void TftpServerWorker::stopServer()
{
    if (!m_server)
        return;
    m_server->stop();
    emit runningChanged(false, tr("Not listening"));
}

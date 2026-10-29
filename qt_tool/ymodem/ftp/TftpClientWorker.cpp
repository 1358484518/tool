#include "TftpClientWorker.h"

#include "TftpClient.h"
#include "TftpProtocol.h"

TftpClientWorker::TftpClientWorker(QObject *parent)
    : QObject(parent)
{
}

TftpClientWorker::~TftpClientWorker()
{
    if (m_client && m_client->isActive())
        m_client->cancel();
}

void TftpClientWorker::ensureClient()
{
    if (m_client)
        return;
    // 这个槽跑在工作线程上。套接字在 start() 里创建，不会生在界面线程。
    m_client = new TftpQt::TftpClient(this);
    connect(m_client, &TftpQt::TftpClient::logMessage, this, &TftpClientWorker::logLine);
    connect(m_client, &TftpQt::TftpClient::progress, this, &TftpClientWorker::progress);
    // finished 可能在 start() 里同步发出。先接上，再调用 start。
    connect(m_client, &TftpQt::TftpClient::finished, this, [this](bool success, const QString &message) {
        emit transferFinished(success, message);
        emit transferActive(false);
    });
}

void TftpClientWorker::startTransfer(QString host, quint16 port, QString remoteFile, QString localFile, bool download)
{
    ensureClient();
    if (m_client->isActive())
        return;

    TftpQt::TftpClient::Request request;
    request.host = host.trimmed();
    request.port = port;
    request.remoteFile = remoteFile.trimmed();
    request.localFile = localFile.trimmed();
    request.download = download;
    request.timeoutSec = 5;
    request.maxRetries = 5;
    // 默认协商以太网块大小、超时和文件大小，避免一直用 512 字节。
    request.blockSize = TftpQt::EthernetBlockSize;
    request.timeoutOption = true;
    request.transferSizeOption = true;

    m_client->start(request);
    emit transferActive(m_client->isActive());
}

void TftpClientWorker::cancelTransfer()
{
    if (m_client)
        m_client->cancel();
}

#include "FtpClientWorker.h"

#include "qftp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

FtpClientWorker::FtpClientWorker(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &FtpClientWorker::onConnectTimeout);
}

FtpClientWorker::~FtpClientWorker()
{
    // 正在析构，不能 deleteLater，也不能再发信号。
    m_timer->stop();
    if (m_file) {
        m_file->close();
        if (m_download)
            m_file->remove();
        delete m_file;
        m_file = nullptr;
    }
    delete m_ftp;
    m_ftp = nullptr;
}

void FtpClientWorker::connectToHost(QString host, quint16 port, QString user, QString password)
{
    dropConnection();
    host = host.trimmed();
    if (host.isEmpty() || port == 0) {
        emit statusMessage(tr("Enter a server and a port."));
        emit connectionClosed();
        return;
    }

    m_host = host;
    // 套接字跟着 QFtp 走。这个槽在工作线程里，所以套接字不会生在界面线程。
    m_ftp = new QFtp(this);
    connect(m_ftp, &QFtp::commandFinished, this, &FtpClientWorker::onCommandFinished);
    connect(m_ftp, &QFtp::listInfo, this, &FtpClientWorker::onListInfo);
    connect(m_ftp, &QFtp::dataTransferProgress, this, &FtpClientWorker::onTransferProgress);

    m_ftp->connectToHost(host, port);
    if (user.isEmpty())
        m_ftp->login();
    else
        m_ftp->login(user, password);

    emit statusMessage(tr("Connecting to %1:%2...").arg(host).arg(port));
    m_timer->start(12000);
}

void FtpClientWorker::disconnectFromHost()
{
    dropConnection();
    emit statusMessage(tr("Disconnected."));
}

void FtpClientWorker::refreshList()
{
    if (!m_ftp || m_file || m_ftp->state() != QFtp::LoggedIn)
        return;
    emit listingStarted();
    emit statusMessage(tr("Listing..."));
    m_ftp->list();
}

void FtpClientWorker::enterDirectory(QString name)
{
    if (!m_ftp || m_file)
        return;
    emit listingStarted();
    emit statusMessage(tr("Opening %1...").arg(name));
    m_ftp->cd(name);
    m_ftp->list();
}

void FtpClientWorker::cdToParent(QString path)
{
    if (!m_ftp || m_file || m_ftp->state() != QFtp::LoggedIn)
        return;
    emit listingStarted();
    emit statusMessage(tr("Opening %1...").arg(path));
    m_ftp->cd(path);
    m_ftp->list();
}

void FtpClientWorker::download(QString remoteName, QString localDir)
{
    if (!m_ftp || m_file || remoteName.isEmpty())
        return;
    QDir dir(localDir.trimmed());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        emit statusMessage(tr("Cannot create %1").arg(dir.absolutePath()));
        return;
    }
    auto *out = new QFile(dir.filePath(remoteName), this);
    if (!out->open(QIODevice::WriteOnly)) {
        emit statusMessage(tr("Cannot write %1: %2").arg(out->fileName(), out->errorString()));
        delete out;
        return;
    }
    m_file = out;
    m_download = true;
    emit transferActive(true);
    emit statusMessage(tr("Downloading %1...").arg(remoteName));
    m_ftp->get(remoteName, m_file, QFtp::Binary);
}

void FtpClientWorker::upload(QString localPath)
{
    if (!m_ftp || m_file || m_ftp->state() != QFtp::LoggedIn)
        return;
    auto *in = new QFile(localPath, this);
    if (!in->open(QIODevice::ReadOnly)) {
        emit statusMessage(tr("Cannot read %1: %2").arg(localPath, in->errorString()));
        delete in;
        return;
    }
    m_file = in;
    m_download = false;
    emit transferActive(true);
    const QString remote = QFileInfo(localPath).fileName();
    emit statusMessage(tr("Uploading %1...").arg(remote));
    m_ftp->put(m_file, remote, QFtp::Binary);
}

void FtpClientWorker::cancelTransfer()
{
    if (!m_ftp || !m_file)
        return;
    emit statusMessage(tr("Canceling transfer..."));
    m_ftp->abort();
}

void FtpClientWorker::onCommandFinished(int, bool error)
{
    if (!m_ftp)
        return;
    const QFtp::Command cmd = m_ftp->currentCommand();
    if (cmd == QFtp::ConnectToHost) {
        m_timer->stop();
        if (error) {
            emit statusMessage(tr("Connect failed: %1").arg(m_ftp->errorString()));
            scheduleDrop();
        }
        return;
    }
    if (cmd == QFtp::Login) {
        m_timer->stop();
        if (error) {
            emit statusMessage(tr("Login failed: %1").arg(m_ftp->errorString()));
            scheduleDrop();
            return;
        }
        emit loggedIn();
        emit statusMessage(tr("Logged in. Listing / ..."));
        emit listingStarted();
        m_ftp->list();
        return;
    }
    if (cmd == QFtp::Get || cmd == QFtp::Put) {
        finishTransfer(error);
        if (cmd == QFtp::Put && !error)
            refreshList();
        return;
    }
    if (cmd == QFtp::List) {
        emit listFinished();
        return;
    }
    if (cmd == QFtp::Cd && error) {
        emit statusMessage(tr("Cannot open directory: %1").arg(m_ftp->errorString()));
        emit directoryFailed();
    }
}

void FtpClientWorker::onListInfo(const QUrlInfo &info)
{
    const QString name = info.name();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return;
    const QString time = info.lastModified().isValid()
                             ? info.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                             : QString();
    emit listItem(name, info.isDir() ? -1 : info.size(), info.owner(), info.group(), time, info.isDir());
}

void FtpClientWorker::onTransferProgress(qint64 done, qint64 total)
{
    emit transferProgress(done, total);
}

void FtpClientWorker::onConnectTimeout()
{
    if (!m_ftp)
        return;
    const QFtp::State state = m_ftp->state();
    if (state == QFtp::HostLookup || state == QFtp::Connecting || state == QFtp::Unconnected) {
        emit statusMessage(tr("Connection to %1 timed out.").arg(m_host));
        scheduleDrop();
    }
}

void FtpClientWorker::dropConnection()
{
    m_timer->stop();
    resetTransfer(m_download);
    if (!m_ftp)
        return;
    QFtp *old = m_ftp;
    m_ftp = nullptr;
    old->abort();
    old->deleteLater();
    emit connectionClosed();
}

void FtpClientWorker::scheduleDrop()
{
    // commandFinished 还在 QFtp 里面。等它返回再删，避免重入。
    QTimer::singleShot(0, this, &FtpClientWorker::dropConnection);
}

void FtpClientWorker::resetTransfer(bool removePartial)
{
    if (!m_file)
        return;
    const QString path = m_file->fileName();
    m_file->close();
    if (removePartial && m_download)
        m_file->remove();
    delete m_file;
    m_file = nullptr;
    Q_UNUSED(path);
    emit transferActive(false);
}

void FtpClientWorker::finishTransfer(bool error)
{
    if (!m_file)
        return;
    const QString path = m_file->fileName();
    const qint64 size = m_download ? m_file->size() : QFileInfo(path).size();
    const bool download = m_download;
    m_file->close();
    if (error && download)
        m_file->remove();
    delete m_file;
    m_file = nullptr;
    emit transferActive(false);
    if (error)
        emit statusMessage(tr("Transfer failed: %1").arg(m_ftp ? m_ftp->errorString() : QString()));
    else if (download)
        emit statusMessage(tr("Saved %1 (%2 bytes)").arg(path).arg(size));
    else
        emit statusMessage(tr("Uploaded %1 (%2 bytes)").arg(QFileInfo(path).fileName()).arg(size));
}

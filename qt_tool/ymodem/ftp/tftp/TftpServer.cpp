#include "TftpServer.h"

#include "TftpProtocol.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QTimer>
#include <QUdpSocket>

#include <functional>

namespace TftpQt {

namespace {

QString endpointKey(const QHostAddress &address, quint16 port)
{
    return address.toString() + QLatin1Char(':') + QString::number(port);
}

bool modeIsOctet(const QByteArray &mode)
{
    return QString::fromLatin1(mode).compare(QLatin1String("octet"), Qt::CaseInsensitive) == 0;
}

} // namespace

class TftpServer::Session : public QObject {
public:
    Session(const QHostAddress &peerAddress, quint16 remotePort, const QString &filePath, bool sendingFile,
            const Packet &request, const Settings &settings, QObject *parent)
        : QObject(parent)
        , peer(peerAddress)
        , peerPort(remotePort)
        , path(filePath)
        , sending(sendingFile)
        , timeoutSec(settings.timeoutSec)
        , maxRetries(settings.maxRetries)
        , timer(this)
    {
        key = endpointKey(peer, peerPort);
        file.setFileName(path);
        oack = negotiate(request, settings, sendingFile);
    }

    QString key;
    std::function<void(const QString &)> onLog;
    std::function<void(Session *)> onFinished;

    bool inDally() const
    {
        return phase == Phase::Dally;
    }

    void start()
    {
        socket = new QUdpSocket(this);
        if (!socket->bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0),
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            finish(false, QString::fromUtf8(u8"无法绑定传输端口"));
            return;
        }
        connect(socket, &QUdpSocket::readyRead, this, [this]() { readPending(); });
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this]() { onTimeout(); });
        if (!oack.isEmpty() && onLog) {
            onLog(QString::fromUtf8(u8"协商块大小 %1，超时 %2 秒").arg(blockSize).arg(timeoutSec));
        }

        if (!file.open(sending ? QIODevice::ReadOnly : (QIODevice::WriteOnly | QIODevice::Truncate))) {
            finish(false, QString::fromUtf8(u8"无法打开文件"));
            return;
        }

        if (sending) {
            if (!oack.isEmpty()) {
                waitingAck0 = true;
                phase = Phase::Send;
                transmit(oack);
            } else {
                sendNextData();
            }
        } else {
            phase = Phase::Recv;
            expectBlock = 1;
            transmit(oack.isEmpty() ? encodeAck(0) : oack);
        }
    }

    void retransmit()
    {
        if (ended || lastPacket.isEmpty())
            return;
        attempt = 0;
        transmit(lastPacket);
    }

    void finishSilent()
    {
        if (ended)
            return;
        ended = true;
        timer.stop();
        if (file.isOpen())
            file.close();
        if (socket)
            socket->close();
        if (onFinished)
            onFinished(this);
    }

    void cancel()
    {
        if (ended)
            return;
        if (phase == Phase::Dally) {
            finishSilent();
            return;
        }
        if (socket && !lastPacket.isEmpty())
            socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "server stopped"), peer, peerPort);
        finish(false, QString::fromUtf8(u8"服务已停止"));
    }

private:
    enum class Phase { Recv, Send, Dally };

    QByteArray negotiate(const Packet &request, const Settings &settings, bool sendingFile)
    {
        QVector<Option> accepted;
        bool sawBlockSize = false;
        bool sawTimeout = false;
        bool sawSize = false;
        for (const Option &option : request.options) {
            if (optionNameIs(option.name, "blksize")) {
                if (sawBlockSize || !settings.allowBlockSize)
                    continue;
                bool ok = false;
                const int value = option.value.toInt(&ok);
                const int limit = qBound(MinBlockSize, settings.maxBlockSize, MaxBlockSize);
                if (ok && value >= MinBlockSize && value <= MaxBlockSize && value <= limit) {
                    blockSize = value;
                    accepted.append({QByteArrayLiteral("blksize"), QByteArray::number(value)});
                    sawBlockSize = true;
                }
            } else if (optionNameIs(option.name, "timeout")) {
                if (sawTimeout || !settings.allowTimeout)
                    continue;
                bool ok = false;
                const int value = option.value.toInt(&ok);
                if (ok && value >= 1 && value <= 255) {
                    timeoutSec = value;
                    accepted.append({QByteArrayLiteral("timeout"), QByteArray::number(value)});
                    sawTimeout = true;
                }
            } else if (optionNameIs(option.name, "tsize")) {
                if (sawSize || !settings.allowTransferSize)
                    continue;
                bool ok = false;
                const qint64 value = option.value.toLongLong(&ok);
                if (!ok || value < 0)
                    continue;
                const qint64 announced = sendingFile ? QFileInfo(path).size() : value;
                accepted.append({QByteArrayLiteral("tsize"), QByteArray::number(announced)});
                sawSize = true;
            }
        }
        if (accepted.isEmpty())
            return {};
        return encodeOack(accepted);
    }

    void readPending()
    {
        if (!socket || ended)
            return;
        while (socket->hasPendingDatagrams()) {
            const qint64 pending = socket->pendingDatagramSize();
            if (pending <= 0)
                break;
            QByteArray datagram;
            datagram.resize(int(pending));
            QHostAddress sender;
            quint16 senderPort = 0;
            const qint64 got = socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
            if (got < 0)
                break;
            datagram.resize(int(got));
            handle(datagram, sender, senderPort);
            if (ended)
                return;
        }
    }

    void handle(const QByteArray &datagram, const QHostAddress &sender, quint16 senderPort)
    {
        if (!samePeer(sender, peer) || senderPort != peerPort) {
            socket->writeDatagram(
                encodeError(quint16(ErrorCode::UnknownTransferId), "unknown transfer id"),
                sender,
                senderPort);
            return;
        }

        const Packet packet = decodePacket(datagram);
        if (!packet.valid)
            return;
        if (packet.opcode == Opcode::Error) {
            const QString detail = packet.errorMessage.isEmpty()
                ? errorCodeName(packet.errorCode)
                : QString::fromUtf8(packet.errorMessage);
            finish(false, QString::fromUtf8(u8"对端错误：%1").arg(detail));
            return;
        }

        if (phase == Phase::Dally) {
            if (packet.opcode == Opcode::Data && packet.block == lastBlock) {
                attempt = 0;
                transmit(lastPacket);
            }
            return;
        }

        if (sending)
            handleSend(packet);
        else
            handleReceive(packet);
    }

    void handleReceive(const Packet &packet)
    {
        if (packet.opcode == Opcode::Oack || (packet.opcode == Opcode::Ack && bytesTransferred == 0))
            return;
        if (packet.opcode != Opcode::Data)
            return;
        if (packet.block != expectBlock) {
            if (expectBlock != 1 && packet.block == quint16(expectBlock - 1)) {
                attempt = 0;
                transmit(lastPacket);
            }
            return;
        }
        if (packet.data.size() > blockSize) {
            socket->writeDatagram(encodeError(quint16(ErrorCode::IllegalOperation), "block too large"), peer, peerPort);
            finish(false, QString::fromUtf8(u8"数据块超过协商大小"));
            return;
        }
        if (file.write(packet.data) != packet.data.size() || !file.flush()) {
            socket->writeDatagram(encodeError(quint16(ErrorCode::DiskFull), "disk full"), peer, peerPort);
            finish(false, QString::fromUtf8(u8"写入文件失败"));
            return;
        }

        bytesTransferred += packet.data.size();
        lastBlock = packet.block;
        const bool finishedBlock = packet.data.size() < blockSize;
        if (!finishedBlock)
            expectBlock = quint16(expectBlock + 1);
        attempt = 0;
        if (finishedBlock) {
            file.close();
            if (!transmit(encodeAck(packet.block)))
                return;
            phase = Phase::Dally;
            timer.start(timeoutSec * 1000);
            if (onLog) {
                onLog(QString::fromUtf8(u8"%1 接收完成，%2 字节")
                          .arg(QFileInfo(path).fileName())
                          .arg(bytesTransferred));
            }
            return;
        }
        transmit(encodeAck(packet.block));
    }

    void handleSend(const Packet &packet)
    {
        if (packet.opcode == Opcode::Oack)
            return;
        if (packet.opcode != Opcode::Ack)
            return;
        if (waitingAck0) {
            if (packet.block == 0) {
                waitingAck0 = false;
                sendNextData();
            }
            return;
        }
        if (packet.block == lastBlock) {
            if (shortSent)
                finish(true, QString::fromUtf8(u8"发送完成，%1 字节").arg(bytesTransferred));
            else
                sendNextData();
            return;
        }
        if (sentData && packet.block == quint16(lastBlock - 1)) {
            attempt = 0;
            transmit(lastPacket);
        }
    }

    void sendNextData()
    {
        if (ended)
            return;
        QByteArray chunk;
        chunk.resize(blockSize);
        const qint64 read = file.read(chunk.data(), blockSize);
        if (read < 0) {
            socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "read error"), peer, peerPort);
            finish(false, QString::fromUtf8(u8"读取文件失败"));
            return;
        }
        chunk.resize(int(read));
        lastBlock = quint16(lastBlock + 1);
        shortSent = chunk.size() < blockSize;
        sentData = true;
        waitingAck0 = false;
        phase = Phase::Send;
        bytesTransferred += chunk.size();
        attempt = 0;
        transmit(encodeData(lastBlock, chunk));
    }

    void onTimeout()
    {
        if (ended)
            return;
        if (phase == Phase::Dally) {
            finishSilent();
            return;
        }
        if (attempt >= maxRetries) {
            if (socket)
                socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "timeout"), peer, peerPort);
            finish(false, QString::fromUtf8(u8"超时"));
            return;
        }
        ++attempt;
        transmit(lastPacket);
    }

    bool transmit(const QByteArray &datagram)
    {
        if (ended || !socket)
            return false;
        lastPacket = datagram;
        const qint64 sent = socket->writeDatagram(datagram, peer, peerPort);
        if (sent != datagram.size()) {
            finish(false, QString::fromUtf8(u8"发送失败"));
            return false;
        }
        if (phase != Phase::Dally)
            timer.start(qMax(1, timeoutSec) * 1000);
        return true;
    }

    void finish(bool success, const QString &message)
    {
        if (ended)
            return;
        ended = true;
        timer.stop();
        if (file.isOpen())
            file.close();
        if (socket)
            socket->close();
        if (onLog) {
            onLog(QString::fromUtf8(success ? u8"%1 %2" : u8"失败 %1：%2").arg(QFileInfo(path).fileName(), message));
        }
        if (onFinished)
            onFinished(this);
    }

    QHostAddress peer;
    quint16 peerPort = 0;
    QString path;
    bool sending = false;
    bool ended = false;
    bool waitingAck0 = false;
    bool sentData = false;
    bool shortSent = false;
    int timeoutSec = 5;
    int maxRetries = 5;
    int attempt = 0;
    int blockSize = DefaultBlockSize;
    Phase phase = Phase::Recv;
    quint16 lastBlock = 0;
    quint16 expectBlock = 1;
    qint64 bytesTransferred = 0;
    QByteArray oack;
    QByteArray lastPacket;
    QFile file;
    QUdpSocket *socket = nullptr;
    QTimer timer;
};

TftpServer::TftpServer(QObject *parent)
    : QObject(parent)
{
}

TftpServer::~TftpServer()
{
    stop();
}

bool TftpServer::isRunning() const
{
    return running;
}

quint16 TftpServer::port() const
{
    return boundPort;
}

int TftpServer::sessionCount() const
{
    return sessions.size();
}

bool TftpServer::start(const Settings &next, QString *errorMessage)
{
    if (running) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(u8"已经在监听");
        return false;
    }
    if (next.timeoutSec < 1 || next.timeoutSec > 255) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(u8"超时需要在 1 到 255 秒之间");
        return false;
    }
    if (next.maxRetries < 0 || next.maxRetries > 30) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(u8"重传次数需要在 0 到 30 之间");
        return false;
    }
    if (next.maxBlockSize < MinBlockSize || next.maxBlockSize > MaxBlockSize) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(u8"最大块大小无效");
        return false;
    }
    const QFileInfo rootInfo(next.root);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(u8"根目录不存在");
        return false;
    }

    settings = next;
    settings.root = rootInfo.canonicalFilePath();
    listenSocket = new QUdpSocket(this);
    connect(listenSocket, &QUdpSocket::readyRead, this, &TftpServer::onReadyRead);
    const auto bindMode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    if (!listenSocket->bind(QHostAddress(QHostAddress::AnyIPv4), settings.port, bindMode)) {
        const QString detail = listenSocket->errorString();
        listenSocket->deleteLater();
        listenSocket = nullptr;
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(u8"绑定端口 %1 失败：%2").arg(settings.port).arg(detail);
        }
        return false;
    }

    boundPort = listenSocket->localPort();
    running = true;
    emit logMessage(QString::fromUtf8(u8"正在监听 %1，根目录 %2").arg(boundPort).arg(settings.root));
    emit sessionCountChanged(0);
    return true;
}

void TftpServer::stop()
{
    if (!running && !listenSocket && sessions.isEmpty())
        return;
    running = false;
    if (listenSocket) {
        listenSocket->disconnect(this);
        listenSocket->close();
        listenSocket->deleteLater();
        listenSocket = nullptr;
    }
    const QList<Session *> active = sessions.values();
    for (Session *session : active)
        session->cancel();
    sessions.clear();
    boundPort = 0;
    emit logMessage(QString::fromUtf8(u8"已停止监听"));
    emit sessionCountChanged(0);
}

void TftpServer::onReadyRead()
{
    if (!listenSocket)
        return;
    while (listenSocket->hasPendingDatagrams()) {
        const qint64 pending = listenSocket->pendingDatagramSize();
        if (pending <= 0)
            break;
        QByteArray datagram;
        datagram.resize(int(pending));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 got = listenSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (got < 0)
            break;
        datagram.resize(int(got));
        takeRequest(datagram, sender, senderPort);
    }
}

void TftpServer::takeRequest(const QByteArray &datagram, const QHostAddress &sender, quint16 senderPort)
{
    const Packet packet = decodePacket(datagram);
    if (!packet.valid || (packet.opcode != Opcode::Rrq && packet.opcode != Opcode::Wrq)) {
        if (packet.valid && packet.opcode == Opcode::Error)
            return;
        sendError(sender, senderPort, quint16(ErrorCode::IllegalOperation), "illegal operation");
        emit logMessage(QString::fromUtf8(u8"拒绝 %1:%2 的非法请求").arg(sender.toString()).arg(senderPort));
        return;
    }
    if (!modeIsOctet(packet.mode)) {
        sendError(sender, senderPort, quint16(ErrorCode::IllegalOperation), "octet only");
        emit logMessage(QString::fromUtf8(u8"拒绝非 octet 模式"));
        return;
    }

    const QString key = endpointKey(sender, senderPort);
    if (Session *existing = sessions.value(key, nullptr)) {
        if (existing->inDally())
            existing->finishSilent();
        else {
            existing->retransmit();
            return;
        }
    }

    QString pathError;
    const QString path = safeChildPath(settings.root, packet.filename, &pathError);
    if (path.isEmpty()) {
        sendError(sender, senderPort, quint16(ErrorCode::AccessViolation), "illegal filename");
        emit logMessage(QString::fromUtf8(u8"拒绝文件名：%1").arg(pathError));
        return;
    }

    const bool reading = packet.opcode == Opcode::Rrq;
    const QFileInfo info(path);
    if (reading) {
        if (!info.isFile()) {
            sendError(sender, senderPort, quint16(ErrorCode::FileNotFound), "file not found");
            emit logMessage(QString::fromUtf8(u8"文件不存在：%1").arg(QString::fromUtf8(packet.filename)));
            return;
        }
    } else if (info.exists() && !info.isFile()) {
        sendError(sender, senderPort, quint16(ErrorCode::AccessViolation), "not a file");
        emit logMessage(QString::fromUtf8(u8"拒绝写入：%1").arg(QString::fromUtf8(packet.filename)));
        return;
    }

    auto *session = new Session(sender, senderPort, path, reading, packet, settings, this);
    session->onLog = [this](const QString &message) { emit logMessage(message); };
    session->onFinished = [this](Session *done) { forgetSession(done); };
    sessions.insert(session->key, session);
    emit logMessage(QString::fromUtf8(reading ? u8"RRQ %1:%2 %3" : u8"WRQ %1:%2 %3")
                        .arg(sender.toString())
                        .arg(senderPort)
                        .arg(QFileInfo(path).fileName()));
    emit sessionCountChanged(sessions.size());
    session->start();
}

void TftpServer::sendError(const QHostAddress &sender, quint16 senderPort, quint16 code, const QByteArray &message)
{
    auto *udp = new QUdpSocket(this);
    if (!udp->bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0))) {
        udp->deleteLater();
        return;
    }
    udp->writeDatagram(encodeError(code, message), sender, senderPort);
    udp->deleteLater();
}

void TftpServer::forgetSession(Session *session)
{
    if (!session)
        return;
    if (sessions.value(session->key) == session)
        sessions.remove(session->key);
    session->deleteLater();
    emit sessionCountChanged(sessions.size());
}

} // namespace TftpQt
